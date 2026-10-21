#pragma once

#include <QList>
#include <QSet>
#include <optional>

#include "core/sort/config.hpp"
#include "core/sort/models.hpp"

namespace px::modrim::sort {

// Assigns each canonical id to a Tier from load order and dependency
// constraints. Ids outside `canonical_ids` are never returned.
QHash<PackageId, Tier> assignTiers(const QSet<PackageId>& canonicalIds,
                                   const PackageGraph& deps,
                                   const PackageGraph& revDeps,
                                   const QSet<PackageId>& loadFirst,
                                   const QSet<PackageId>& loadLast,
                                   const TierConfig& tierConfig);

}  // namespace px::modrim::sort
