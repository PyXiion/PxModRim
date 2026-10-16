#pragma once

#include <QString>

#include "core/error.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/sort/models.hpp"

namespace px::modrim::sort {

// Converts the upstream schema into rules keyed by package id. Only the keys of loadAfter and
// loadBefore matter; their values are ignored.
CommunityRules toCommunityRules(const ExternalRulesSchema& schema);

// Reads the community rules file. A missing file means no rules; a malformed one is an error.
Result<CommunityRules> loadCommunityRules(const QString& jsonPath);

}  // namespace px::modrim::sort
