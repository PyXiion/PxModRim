#pragma once

#include <optional>

#include <QString>

#include "core/services/startup_impact_service/models.h"

namespace px::core::startup_impact {

// The report lives next to the game's config folder, not inside it.
QString get_startup_impact_path(const QString& config_folder);

// Reads a StartupImpactData.json report. Returns nullopt when the file is missing, unreadable,
// or not a JSON object. Malformed entries are skipped rather than failing the whole report.
std::optional<StartupImpactReport> parse_startup_impact(const QString& path);

}  // namespace px::core::startup_impact
