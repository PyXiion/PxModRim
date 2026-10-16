#include "core/sort/tiers.hpp"

#include <algorithm>
#include <vector>

namespace px::modrim::sort {

namespace {

// Every node reachable from `start` by at least one edge. `start` is included only when a cycle
// leads back to it.
QSet<PackageId> reachable(const PackageId& start, const PackageGraph& graph) {
    QSet<PackageId> result;
    QSet<PackageId> visited{start};
    std::vector<PackageId> stack{start};
    while (!stack.empty()) {
        const PackageId pid = stack.back();
        stack.pop_back();
        for (const auto& next : graph.value(pid)) {
            result.insert(next);
            if (!visited.contains(next)) {
                visited.insert(next);
                stack.push_back(next);
            }
        }
    }
    return result;
}

// Adds `ids` to `tiers` as `tier`, skipping ids that are not canonical (keys of `tiers`).
void assignPresent(QHash<PackageId, Tier>& tiers, const QSet<PackageId>& ids, Tier tier) {
    for (const auto& pid : ids) {
        if (auto it = tiers.find(pid); it != tiers.end()) {
            *it = tier;
        }
    }
}

}  // namespace

QHash<PackageId, Tier> assignTiers(const QSet<PackageId>& canonicalIds,
                                    const PackageGraph& deps,
                                    const PackageGraph& revDeps,
                                    const QSet<PackageId>& loadFirst,
                                    const QSet<PackageId>& loadLast,
                                    const TierConfig& tierConfig) {
    QHash<PackageId, Tier> tiers;
    for (const auto& pid : canonicalIds) {
        tiers.insert(pid, Tier::kTier2);
    }

    // Tier 3 is the bottom of the load order, so dependents of late mods also sink there.
    QSet<PackageId> tier3Set = loadLast;
    for (const auto& pid : loadLast) {
        tier3Set |= reachable(pid, revDeps);
    }
    for (const auto& pid : tierConfig.tier_3) {
        if (canonicalIds.contains(pid)) {
            tier3Set.insert(pid);
            tier3Set |= reachable(pid, revDeps);
        }
    }
    assignPresent(tiers, tier3Set, Tier::kTier3);

    // Tier 0 is assigned after tier 3, so a configured tier-0 mod wins over a tier-3 mark.
    QSet<PackageId> tier0Set;
    for (const auto& pid : tierConfig.tier_0) {
        if (canonicalIds.contains(pid)) {
            tier0Set.insert(pid);
            tier0Set |= reachable(pid, deps);
        }
    }
    assignPresent(tiers, tier0Set, Tier::kTier0);

    // Only tier-2 mods move up: a tier-0 or tier-3 mark is never overridden here.
    QSet<PackageId> tier1Set;
    for (const auto& pid : tierConfig.tier_1) {
        if (canonicalIds.contains(pid)) {
            tier1Set.insert(pid);
            tier1Set |= reachable(pid, deps);
        }
    }
    for (const auto& pid : loadFirst) {
        if (canonicalIds.contains(pid)) {
            tier1Set.insert(pid);
            tier1Set |= reachable(pid, deps);
        }
    }
    for (const auto& pid : tier1Set) {
        if (auto it = tiers.find(pid); it != tiers.end() && *it == Tier::kTier2) {
            *it = Tier::kTier1;
        }
    }

    return tiers;
}

QSet<PackageId> getDepsRecursive(const PackageId& start, const PackageGraph& deps) {
    return reachable(start, deps);
}

QSet<PackageId> getReverseDepsRecursive(const PackageId& start, const PackageGraph& revDeps) {
    return reachable(start, revDeps);
}

std::optional<QList<PackageId>> findCycle(const QSet<PackageId>& nodes, const PackageGraph& deps) {
    // One frame per node on the DFS stack; `next` indexes the node's outgoing edges.
    struct Frame {
        PackageId pid;
        QList<PackageId> edges;
        qsizetype next = 0;
    };

    QSet<PackageId> visited;
    QSet<PackageId> recStack;
    QHash<PackageId, PackageId> parent;

    for (const auto& root : nodes) {
        if (visited.contains(root)) {
            continue;
        }

        parent.insert(root, root);
        visited.insert(root);
        recStack.insert(root);
        std::vector<Frame> stack{Frame{root, deps.value(root).values()}};
        while (!stack.empty()) {
            Frame& top = stack.back();
            if (top.next == top.edges.size()) {
                recStack.remove(top.pid);
                stack.pop_back();
                continue;
            }

            const PackageId pid = top.pid;
            const PackageId dep = top.edges[top.next++];
            if (!nodes.contains(dep)) {
                continue;
            }
            if (!visited.contains(dep)) {
                parent.insert(dep, pid);
                visited.insert(dep);
                recStack.insert(dep);
                // `top` is invalid after this push.
                stack.push_back(Frame{dep, deps.value(dep).values()});
            } else if (recStack.contains(dep)) {
                // Walk parents from the current node back to `dep`, which is on the DFS path.
                QList<PackageId> cycle{dep};
                PackageId current = pid;
                while (current != dep) {
                    cycle.append(current);
                    current = parent.value(current);
                }
                cycle.append(dep);
                std::ranges::reverse(cycle);
                return cycle;
            }
        }
    }

    return std::nullopt;
}

}  // namespace px::modrim::sort
