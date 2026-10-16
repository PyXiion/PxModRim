#pragma once

#include <vector>

#include "core/models/metadata/structures.hpp"

namespace px::modrim::sort {

using PackageId = CaseInsensitiveStr;

enum class Tier : int { Tier0 = 0, Tier1 = 1, Tier2 = 2, Tier3 = 3 };

// Serialised by glaze into config.json: member names are the JSON keys.
struct TierConfig {
    std::vector<PackageId> tier_0;
    std::vector<PackageId> tier_1;
    std::vector<PackageId> tier_3;

    static TierConfig defaults();

    bool operator==(const TierConfig&) const = default;
};

struct SortSettings {
    bool use_alternative_package_ids = true;
    bool check_missing_dependencies = true;
    bool use_community_rules = true;
    TierConfig tier_config = TierConfig::defaults();

    bool operator==(const SortSettings&) const = default;
};

}  // namespace px::modrim::sort
