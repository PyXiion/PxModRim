#pragma once

#include <optional>

#include <QList>
#include <QSet>

#include "core/sort/config.h"
#include "core/sort/models.h"

namespace px::modrim::sort {

// Assigns each canonical id to a Tier from load order and dependency constraints. Ids outside
// `canonical_ids` are never returned.
QHash<PackageId, Tier> assign_tiers(const QSet<PackageId>& canonical_ids,
                                    const PackageGraph& deps,
                                    const PackageGraph& rev_deps,
                                    const QSet<PackageId>& load_first,
                                    const QSet<PackageId>& load_last,
                                    const TierConfig& tier_config);

// Transitive closure over `deps`, excluding `start` itself unless it is reachable from itself.
QSet<PackageId> get_deps_recursive(const PackageId& start, const PackageGraph& deps);

// Transitive closure over `rev_deps`, excluding `start` itself unless it is reachable from itself.
QSet<PackageId> get_reverse_deps_recursive(const PackageId& start, const PackageGraph& rev_deps);

// Returns one cycle among `nodes` as a closed path (first == last), or nullopt when the nodes
// form a DAG. Edges leaving `nodes` are ignored.
std::optional<QList<PackageId>> find_cycle(const QSet<PackageId>& nodes, const PackageGraph& deps);

}  // namespace px::modrim::sort
