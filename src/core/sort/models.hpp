#pragma once

#include <QHash>
#include <QSet>

#include "core/models/metadata/structures.hpp"
#include "core/sort/config.hpp"

namespace px::modrim::sort {

// Load-order rule from the community database for one package id.
struct CommunityRule {
    PackageId packageId;
    CaseInsensitiveSet loadAfter;
    CaseInsensitiveSet loadBefore;
    bool loadFirst = false;
    bool loadLast = false;
    CaseInsensitiveSet incompatibleWith;

    bool operator==(const CommunityRule&) const = default;
};

using CommunityRules = QHash<PackageId, CommunityRule>;

// Adjacency map over package ids: a node's dependencies or reverse dependencies.
using PackageGraph = QHash<PackageId, QSet<PackageId>>;

}  // namespace px::modrim::sort
