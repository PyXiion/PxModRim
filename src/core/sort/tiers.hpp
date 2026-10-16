#pragma once

#include <optional>

#include <QList>
#include <QSet>

#include "core/sort/config.hpp"
#include "core/sort/models.hpp"

namespace px::modrim::sort {

// Assigns each canonical id to a Tier from load order and dependency constraints. Ids outside
// `canonical_ids` are never returned.
QHash<PackageId, Tier> assignTiers(const QSet<PackageId>& canonicalIds,
                                    const PackageGraph& deps,
                                    const PackageGraph& revDeps,
                                    const QSet<PackageId>& loadFirst,
                                    const QSet<PackageId>& loadLast,
                                    const TierConfig& tierConfig);

// Transitive closure over `deps`, excluding `start` itself unless it is reachable from itself.
QSet<PackageId> getDepsRecursive(const PackageId& start, const PackageGraph& deps);

// Transitive closure over `rev_deps`, excluding `start` itself unless it is reachable from itself.
QSet<PackageId> getReverseDepsRecursive(const PackageId& start, const PackageGraph& revDeps);

// Returns one cycle among `nodes` as a closed path (first == last), or nullopt when the nodes
// form a DAG. Edges leaving `nodes` are ignored.
std::optional<QList<PackageId>> findCycle(const QSet<PackageId>& nodes, const PackageGraph& deps);

}  // namespace px::modrim::sort
