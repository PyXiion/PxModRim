#pragma once

#include <QString>
#include <optional>

#include "core/services/startup_impact_service/models.hpp"

namespace px::modrim::startup_impact {

// The report lives next to the game's config folder, not inside it.
QString getStartupImpactPath(const QString& configFolder);

// Reads a StartupImpactData.json report. Returns nullopt when the file is
// missing, unreadable, or not a JSON object. Malformed entries are skipped
// rather than failing the whole report.
std::optional<StartupImpactReport> parseStartupImpact(const QString& path);

}  // namespace px::modrim::startup_impact
