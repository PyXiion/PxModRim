#pragma once

#include <optional>
#include <QStringList>
#include <QVariantMap>
#include "core/services/startup_impact_service/models.hpp"
#include "ui/models/impact.hpp"

namespace px::modrim {
QVariantMap buildStartupImpact(const startup_impact::StartupImpactReport& report,
                                const QStringList& activePids,
                                const std::optional<QString>& selectedPid, double estimatedS);
} // namespace px::modrim
