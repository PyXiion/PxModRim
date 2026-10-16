#pragma once

#include <vector>

#include "core/models/metadata/structures.hpp"

namespace px::modrim::sort {

using PackageId = CaseInsensitiveStr;

enum class Tier : int { kTier0 = 0, kTier1 = 1, kTier2 = 2, kTier3 = 3 };

// Serialised by glaze into config.json: member names are the JSON keys.
// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or on the wire
struct TierConfig {
    std::vector<PackageId> tier_0;
    std::vector<PackageId> tier_1;
    std::vector<PackageId> tier_3;

    static TierConfig defaults();

    bool operator==(const TierConfig&) const = default;
};
// NOLINTEND(readability-identifier-naming)

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or on the wire
struct SortSettings {
    bool use_alternative_package_ids = true;
    bool check_missing_dependencies = true;
    bool use_community_rules = true;
    TierConfig tier_config = TierConfig::defaults();

    bool operator==(const SortSettings&) const = default;
};
// NOLINTEND(readability-identifier-naming)

}  // namespace px::modrim::sort
