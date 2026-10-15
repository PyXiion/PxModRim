#include "core/sort/tiers.h"

#include <algorithm>
#include <vector>

namespace px::core::sort {

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
void assign_present(QHash<PackageId, Tier>& tiers, const QSet<PackageId>& ids, Tier tier) {
    for (const auto& pid : ids) {
        if (auto it = tiers.find(pid); it != tiers.end()) {
            *it = tier;
        }
    }
}

}  // namespace

QHash<PackageId, Tier> assign_tiers(const QSet<PackageId>& canonical_ids,
                                    const PackageGraph& deps,
                                    const PackageGraph& rev_deps,
                                    const QSet<PackageId>& load_first,
                                    const QSet<PackageId>& load_last,
                                    const TierConfig& tier_config) {
    QHash<PackageId, Tier> tiers;
    for (const auto& pid : canonical_ids) {
        tiers.insert(pid, Tier::Tier2);
    }

    // Tier 3 is the bottom of the load order, so dependents of late mods also sink there.
    QSet<PackageId> tier3_set = load_last;
    for (const auto& pid : load_last) {
        tier3_set |= reachable(pid, rev_deps);
    }
    for (const auto& pid : tier_config.tier_3) {
        if (canonical_ids.contains(pid)) {
            tier3_set.insert(pid);
            tier3_set |= reachable(pid, rev_deps);
        }
    }
    assign_present(tiers, tier3_set, Tier::Tier3);

    // Tier 0 is assigned after tier 3, so a configured tier-0 mod wins over a tier-3 mark.
    QSet<PackageId> tier0_set;
    for (const auto& pid : tier_config.tier_0) {
        if (canonical_ids.contains(pid)) {
            tier0_set.insert(pid);
            tier0_set |= reachable(pid, deps);
        }
    }
    assign_present(tiers, tier0_set, Tier::Tier0);

    // Only tier-2 mods move up: a tier-0 or tier-3 mark is never overridden here.
    QSet<PackageId> tier1_set;
    for (const auto& pid : tier_config.tier_1) {
        if (canonical_ids.contains(pid)) {
            tier1_set.insert(pid);
            tier1_set |= reachable(pid, deps);
        }
    }
    for (const auto& pid : load_first) {
        if (canonical_ids.contains(pid)) {
            tier1_set.insert(pid);
            tier1_set |= reachable(pid, deps);
        }
    }
    for (const auto& pid : tier1_set) {
        if (auto it = tiers.find(pid); it != tiers.end() && *it == Tier::Tier2) {
            *it = Tier::Tier1;
        }
    }

    return tiers;
}

QSet<PackageId> get_deps_recursive(const PackageId& start, const PackageGraph& deps) {
    return reachable(start, deps);
}

QSet<PackageId> get_reverse_deps_recursive(const PackageId& start, const PackageGraph& rev_deps) {
    return reachable(start, rev_deps);
}

std::optional<QList<PackageId>> find_cycle(const QSet<PackageId>& nodes, const PackageGraph& deps) {
    // One frame per node on the DFS stack; `next` indexes the node's outgoing edges.
    struct Frame {
        PackageId pid;
        QList<PackageId> edges;
        qsizetype next = 0;
    };

    QSet<PackageId> visited;
    QSet<PackageId> rec_stack;
    QHash<PackageId, PackageId> parent;

    for (const auto& root : nodes) {
        if (visited.contains(root)) {
            continue;
        }

        parent.insert(root, root);
        visited.insert(root);
        rec_stack.insert(root);
        std::vector<Frame> stack{Frame{root, deps.value(root).values()}};
        while (!stack.empty()) {
            Frame& top = stack.back();
            if (top.next == top.edges.size()) {
                rec_stack.remove(top.pid);
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
                rec_stack.insert(dep);
                // `top` is invalid after this push.
                stack.push_back(Frame{dep, deps.value(dep).values()});
            } else if (rec_stack.contains(dep)) {
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

}  // namespace px::core::sort
