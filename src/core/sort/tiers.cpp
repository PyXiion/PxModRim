#include "core/sort/tiers.hpp"

#include <vector>

namespace px::modrim::sort {

namespace {

// Every node reachable from `start` by at least one edge. `start` is included
// only when a cycle leads back to it.
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

// Adds `ids` to `tiers` as `tier`, skipping ids that are not canonical (keys of
// `tiers`).
void assignPresent(QHash<PackageId, Tier>& tiers, const QSet<PackageId>& ids,
                   Tier tier) {
  for (const auto& pid : ids) {
    if (auto it = tiers.find(pid); it != tiers.end()) {
      *it = tier;
    }
  }
}

// Canonical members of `ids` plus everything reachable from them in `graph`.
QSet<PackageId> closureWithin(const auto& ids, const QSet<PackageId>& canonical,
                              const PackageGraph& graph) {
  QSet<PackageId> result;
  for (const auto& pid : ids) {
    if (canonical.contains(pid)) {
      result.insert(pid);
      result |= reachable(pid, graph);
    }
  }
  return result;
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

  // Tier 3 is the bottom of the load order, so dependents of late mods also
  // sink there.
  assignPresent(tiers,
                closureWithin(loadLast, canonicalIds, revDeps) |
                    closureWithin(tierConfig.tier_3, canonicalIds, revDeps),
                Tier::kTier3);

  // Tier 0 is assigned after tier 3, so a configured tier-0 mod wins over a
  // tier-3 mark.
  assignPresent(tiers, closureWithin(tierConfig.tier_0, canonicalIds, deps),
                Tier::kTier0);

  // Only tier-2 mods move up: a tier-0 or tier-3 mark is never overridden here.
  const QSet<PackageId> tier1Set =
      closureWithin(tierConfig.tier_1, canonicalIds, deps) |
      closureWithin(loadFirst, canonicalIds, deps);
  for (const auto& pid : tier1Set) {
    if (auto it = tiers.find(pid); it != tiers.end() && *it == Tier::kTier2) {
      *it = Tier::kTier1;
    }
  }

  return tiers;
}

}  // namespace px::modrim::sort
