#include "core/checker/sort.hpp"

#include <algorithm>
#include <array>
#include <tuple>
#include <utility>
#include <vector>

#include "core/sort/tiers.hpp"

namespace px::modrim::checker {
namespace {

using sort::PackageGraph;
using sort::Tier;

// Dependency edges point from a mod to the mods that must load before it; load-before edges are
// reversed so both shapes end up in the same map.
std::pair<PackageGraph, PackageGraph> buildDepsAndRevDeps(const ConstraintGraph& graph,
                                                              const QSet<PackageId>& allPids) {
    PackageGraph deps;
    PackageGraph revDeps;
    for (const PackageId& pid : allPids) {
        deps.insert(pid, {});
        revDeps.insert(pid, {});
    }

    for (const PackageId& pid : graph.nodes()) {
        if (!allPids.contains(pid)) {
            continue;
        }
        for (const ConstraintEdge& edge : graph.outgoing(pid)) {
            if (!allPids.contains(edge.target)) {
                continue;
            }
            switch (edge.type) {
                case EdgeType::kLoadBefore:
                    deps[edge.target].insert(pid);
                    revDeps[pid].insert(edge.target);
                    break;
                case EdgeType::kLoadAfter:
                case EdgeType::kDependency:
                    deps[pid].insert(edge.target);
                    revDeps[edge.target].insert(pid);
                    break;
                case EdgeType::kIncompatibility:
                case EdgeType::kAlternative:
                    break;
            }
        }
    }

    return {std::move(deps), std::move(revDeps)};
}

// Mods that community rules flag through `flag`, or none when community rules are disabled.
QSet<PackageId> communityFlagged(const sort::CommunityRules* rules,
                                  const QSet<PackageId>& allPids,
                                  const sort::SortSettings& settings,
                                  bool sort::CommunityRule::*flag) {
    QSet<PackageId> flagged;
    if (!settings.use_community_rules || rules == nullptr || rules->isEmpty()) {
        return flagged;
    }
    for (const PackageId& pid : allPids) {
        const auto rule = rules->constFind(pid);
        if (rule != rules->cend() && rule.value().*flag) {
            flagged.insert(pid);
        }
    }
    return flagged;
}

// Position of each package id in the tier config, so the user's explicit picks sort first.
QHash<PackageId, int> buildConfigPriority(const sort::TierConfig& tierConfig) {
    QHash<PackageId, int> priority;
    for (std::size_t i = 0; i < tierConfig.tier_0.size(); ++i) {
        priority.insert(tierConfig.tier_0[i], static_cast<int>(i));
    }
    // Tier 1 and tier 3 only fill in ids that tier 0 did not already place, and they are
    // numbered after the earlier lists.
    int base = static_cast<int>(tierConfig.tier_0.size());
    for (const auto* list : {&tierConfig.tier_1, &tierConfig.tier_3}) {
        for (std::size_t i = 0; i < list->size(); ++i) {
            const PackageId& pid = (*list)[i];
            if (!priority.contains(pid)) {
                priority.insert(pid, base + static_cast<int>(i));
            }
        }
        base += static_cast<int>(list->size());
    }
    return priority;
}

struct SortKey {
    int priority;
    QString name;
    PackageId pid;
};

SortKey makeKey(const PackageId& pid, const QHash<PackageId, int>& configPriority) {
    return SortKey{configPriority.value(pid, static_cast<int>(configPriority.size())),
                   pid.str(),
                   pid};
}

bool keyLess(const SortKey& a, const SortKey& b) {
    if (a.priority != b.priority) {
        return a.priority < b.priority;
    }
    return a.name < b.name;
}

// Heap order that pops the smallest key first.
bool keyGreater(const SortKey& a, const SortKey& b) {
    return keyLess(b, a);
}

}  // namespace

QList<PackageId> topologicalSort(const QHash<PackageId, std::shared_ptr<AboutXmlMod>>& activeMods,
                                  const ConstraintGraph& graph,
                                  const sort::SortSettings& settings,
                                  const sort::CommunityRules* communityRules,
                                  Profiler* timer) {
    Profiler local;
    Profiler& t = timer != nullptr ? *timer : local;

    const QSet<PackageId> allPids(activeMods.keyBegin(), activeMods.keyEnd());
    if (allPids.isEmpty()) {
        return {};
    }

    PackageGraph deps;
    PackageGraph revDeps;
    {
        auto scope = t("build_deps");
        std::tie(deps, revDeps) = buildDepsAndRevDeps(graph, allPids);
    }
    QSet<PackageId> loadFirst;
    {
        auto scope = t("build_load_first");
        loadFirst = communityFlagged(communityRules,
                                       allPids,
                                       settings,
                                       &sort::CommunityRule::loadFirst);
    }
    QSet<PackageId> loadLast;
    {
        auto scope = t("build_load_last");
        loadLast = communityFlagged(communityRules,
                                      allPids,
                                      settings,
                                      &sort::CommunityRule::loadLast);
    }

    QHash<PackageId, Tier> tiers;
    {
        auto scope = t("assign_tiers");
        tiers = sort::assignTiers(allPids,
                                   deps,
                                   revDeps,
                                   loadFirst,
                                   loadLast,
                                   settings.tier_config);
    }

    constexpr std::array<std::pair<Tier, const char*>, 4> kTierPasses{{
        {Tier::kTier0, "kahn_tier_0"},
        {Tier::kTier1, "kahn_tier_1"},
        {Tier::kTier2, "kahn_tier_2"},
        {Tier::kTier3, "kahn_tier_3"},
    }};
    const QHash<PackageId, int> configPriority = buildConfigPriority(settings.tier_config);

    QList<PackageId> order;
    QSet<PackageId> ordered;
    for (const auto& [tier, label] : kTierPasses) {
        QSet<PackageId> tierPids;
        for (const PackageId& pid : allPids) {
            if (ordered.contains(pid)) {
                continue;
            }
            const auto assigned = tiers.constFind(pid);
            if (assigned != tiers.cend() && assigned.value() == tier) {
                tierPids.insert(pid);
            }
        }
        if (tierPids.isEmpty()) {
            continue;
        }

        auto scope = t(label);

        // Dependencies are restricted to this tier: earlier tiers are already placed, and
        // later tiers must not hold up this one.
        QHash<PackageId, int> indegree;
        QHash<PackageId, QSet<PackageId>> rev;
        for (const PackageId& pid : tierPids) {
            QSet<PackageId> subDeps = deps.value(pid);
            subDeps.intersect(tierPids);
            indegree.insert(pid, static_cast<int>(subDeps.size()));
            for (const PackageId& dep : subDeps) {
                rev[dep].insert(pid);
            }
        }

        std::vector<SortKey> queue;
        for (const PackageId& pid : tierPids) {
            if (indegree.value(pid) == 0) {
                queue.push_back(makeKey(pid, configPriority));
            }
        }
        std::ranges::make_heap(queue, keyGreater);

        while (!queue.empty()) {
            std::ranges::pop_heap(queue, keyGreater);
            const PackageId pid = queue.back().pid;
            queue.pop_back();
            order.append(pid);
            ordered.insert(pid);

            for (const PackageId& dependent : rev.value(pid)) {
                if (--indegree[dependent] == 0) {
                    queue.push_back(makeKey(dependent, configPriority));
                    std::ranges::push_heap(queue, keyGreater);
                }
            }
        }

        // Whatever is left sits on a dependency cycle; keep it deterministic at the tier's end.
        std::vector<SortKey> remaining;
        for (const PackageId& pid : tierPids) {
            if (!ordered.contains(pid)) {
                remaining.push_back(makeKey(pid, configPriority));
            }
        }
        std::ranges::sort(remaining, keyLess);
        for (const SortKey& key : remaining) {
            order.append(key.pid);
            ordered.insert(key.pid);
        }
    }

    return order;
}

}  // namespace px::modrim::checker
