#pragma once

#include <QString>
#include <optional>

#include "core/services/startup_impact_service/models.hpp"

namespace px::modrim::startup_impact {

QString getStartupImpactPath(const QString& configFolder);
std::optional<StartupImpactReport> parseStartupImpact(const QString& path);

}  // namespace px::modrim::startup_impact
