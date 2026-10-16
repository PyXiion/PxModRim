#include "core/checker/sort.h"

#include <algorithm>
#include <array>
#include <tuple>
#include <utility>
#include <vector>

#include "core/sort/tiers.h"

namespace px::modrim::checker {
namespace {

using sort::PackageGraph;
using sort::Tier;

// Dependency edges point from a mod to the mods that must load before it; load-before edges are
// reversed so both shapes end up in the same map.
std::pair<PackageGraph, PackageGraph> build_deps_and_rev_deps(const ConstraintGraph& graph,
                                                              const QSet<PackageId>& all_pids) {
    PackageGraph deps;
    PackageGraph rev_deps;
    for (const PackageId& pid : all_pids) {
        deps.insert(pid, {});
        rev_deps.insert(pid, {});
    }

    for (const PackageId& pid : graph.nodes()) {
        if (!all_pids.contains(pid)) {
            continue;
        }
        for (const ConstraintEdge& edge : graph.outgoing(pid)) {
            if (!all_pids.contains(edge.target)) {
                continue;
            }
            switch (edge.type) {
                case EdgeType::LoadBefore:
                    deps[edge.target].insert(pid);
                    rev_deps[pid].insert(edge.target);
                    break;
                case EdgeType::LoadAfter:
                case EdgeType::Dependency:
                    deps[pid].insert(edge.target);
                    rev_deps[edge.target].insert(pid);
                    break;
                case EdgeType::Incompatibility:
                case EdgeType::Alternative:
                    break;
            }
        }
    }

    return {std::move(deps), std::move(rev_deps)};
}

// Mods that community rules flag through `flag`, or none when community rules are disabled.
QSet<PackageId> community_flagged(const sort::CommunityRules* rules,
                                  const QSet<PackageId>& all_pids,
                                  const sort::SortSettings& settings,
                                  bool sort::CommunityRule::*flag) {
    QSet<PackageId> flagged;
    if (!settings.use_community_rules || rules == nullptr || rules->isEmpty()) {
        return flagged;
    }
    for (const PackageId& pid : all_pids) {
        const auto rule = rules->constFind(pid);
        if (rule != rules->cend() && rule.value().*flag) {
            flagged.insert(pid);
        }
    }
    return flagged;
}

// Position of each package id in the tier config, so the user's explicit picks sort first.
QHash<PackageId, int> build_config_priority(const sort::TierConfig& tier_config) {
    QHash<PackageId, int> priority;
    for (std::size_t i = 0; i < tier_config.tier_0.size(); ++i) {
        priority.insert(tier_config.tier_0[i], static_cast<int>(i));
    }
    // Tier 1 and tier 3 only fill in ids that tier 0 did not already place, and they are
    // numbered after the earlier lists.
    int base = static_cast<int>(tier_config.tier_0.size());
    for (const auto* list : {&tier_config.tier_1, &tier_config.tier_3}) {
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

SortKey make_key(const PackageId& pid, const QHash<PackageId, int>& config_priority) {
    return SortKey{config_priority.value(pid, static_cast<int>(config_priority.size())),
                   pid.str(),
                   pid};
}

bool key_less(const SortKey& a, const SortKey& b) {
    if (a.priority != b.priority) {
        return a.priority < b.priority;
    }
    return a.name < b.name;
}

// Heap order that pops the smallest key first.
bool key_greater(const SortKey& a, const SortKey& b) {
    return key_less(b, a);
}

}  // namespace

QList<PackageId> topological_sort(const QHash<PackageId, std::shared_ptr<AboutXmlMod>>& active_mods,
                                  const ConstraintGraph& graph,
                                  const sort::SortSettings& settings,
                                  const sort::CommunityRules* community_rules,
                                  Profiler* timer) {
    Profiler local;
    Profiler& t = timer != nullptr ? *timer : local;

    const QSet<PackageId> all_pids(active_mods.keyBegin(), active_mods.keyEnd());
    if (all_pids.isEmpty()) {
        return {};
    }

    PackageGraph deps;
    PackageGraph rev_deps;
    {
        auto scope = t("build_deps");
        std::tie(deps, rev_deps) = build_deps_and_rev_deps(graph, all_pids);
    }
    QSet<PackageId> load_first;
    {
        auto scope = t("build_load_first");
        load_first = community_flagged(community_rules,
                                       all_pids,
                                       settings,
                                       &sort::CommunityRule::load_first);
    }
    QSet<PackageId> load_last;
    {
        auto scope = t("build_load_last");
        load_last = community_flagged(community_rules,
                                      all_pids,
                                      settings,
                                      &sort::CommunityRule::load_last);
    }

    QHash<PackageId, Tier> tiers;
    {
        auto scope = t("assign_tiers");
        tiers = sort::assign_tiers(all_pids,
                                   deps,
                                   rev_deps,
                                   load_first,
                                   load_last,
                                   settings.tier_config);
    }

    constexpr std::array<std::pair<Tier, const char*>, 4> tier_passes{{
        {Tier::Tier0, "kahn_tier_0"},
        {Tier::Tier1, "kahn_tier_1"},
        {Tier::Tier2, "kahn_tier_2"},
        {Tier::Tier3, "kahn_tier_3"},
    }};
    const QHash<PackageId, int> config_priority = build_config_priority(settings.tier_config);

    QList<PackageId> order;
    QSet<PackageId> ordered;
    for (const auto& [tier, label] : tier_passes) {
        QSet<PackageId> tier_pids;
        for (const PackageId& pid : all_pids) {
            if (ordered.contains(pid)) {
                continue;
            }
            const auto assigned = tiers.constFind(pid);
            if (assigned != tiers.cend() && assigned.value() == tier) {
                tier_pids.insert(pid);
            }
        }
        if (tier_pids.isEmpty()) {
            continue;
        }

        auto scope = t(label);

        // Dependencies are restricted to this tier: earlier tiers are already placed, and
        // later tiers must not hold up this one.
        QHash<PackageId, int> indegree;
        QHash<PackageId, QSet<PackageId>> rev;
        for (const PackageId& pid : tier_pids) {
            QSet<PackageId> sub_deps = deps.value(pid);
            sub_deps.intersect(tier_pids);
            indegree.insert(pid, static_cast<int>(sub_deps.size()));
            for (const PackageId& dep : sub_deps) {
                rev[dep].insert(pid);
            }
        }

        std::vector<SortKey> queue;
        for (const PackageId& pid : tier_pids) {
            if (indegree.value(pid) == 0) {
                queue.push_back(make_key(pid, config_priority));
            }
        }
        std::ranges::make_heap(queue, key_greater);

        while (!queue.empty()) {
            std::ranges::pop_heap(queue, key_greater);
            const PackageId pid = queue.back().pid;
            queue.pop_back();
            order.append(pid);
            ordered.insert(pid);

            for (const PackageId& dependent : rev.value(pid)) {
                if (--indegree[dependent] == 0) {
                    queue.push_back(make_key(dependent, config_priority));
                    std::ranges::push_heap(queue, key_greater);
                }
            }
        }

        // Whatever is left sits on a dependency cycle; keep it deterministic at the tier's end.
        std::vector<SortKey> remaining;
        for (const PackageId& pid : tier_pids) {
            if (!ordered.contains(pid)) {
                remaining.push_back(make_key(pid, config_priority));
            }
        }
        std::ranges::sort(remaining, key_less);
        for (const SortKey& key : remaining) {
            order.append(key.pid);
            ordered.insert(key.pid);
        }
    }

    return order;
}

}  // namespace px::modrim::checker
