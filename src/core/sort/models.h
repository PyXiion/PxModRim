#pragma once

#include <QHash>
#include <QSet>

#include "core/models/metadata/structures.h"
#include "core/sort/config.h"

namespace px::core::sort {

// Load-order rule from the community database for one package id.
struct CommunityRule {
    PackageId package_id;
    CaseInsensitiveSet load_after;
    CaseInsensitiveSet load_before;
    bool load_first = false;
    bool load_last = false;
    CaseInsensitiveSet incompatible_with;

    bool operator==(const CommunityRule&) const = default;
};

using CommunityRules = QHash<PackageId, CommunityRule>;

// Adjacency map over package ids: a node's dependencies or reverse dependencies.
using PackageGraph = QHash<PackageId, QSet<PackageId>>;

}  // namespace px::core::sort
