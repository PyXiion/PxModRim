#pragma once

#include <QList>
#include <QMap>
#include <QStringList>
#include <QVariantMap>
#include <optional>
#include <utility>

#include "core/services/startup_impact_service/models.hpp"
#include "ui/models/impact.hpp"

namespace px::modrim {
constexpr double kMinSeconds = 0.001;
// Metrics by descending value.
QList<std::pair<QString, double>> sortedMetrics(
    const QMap<QString, double>& metrics);
QVariantMap buildStartupImpact(
    const startup_impact::StartupImpactReport& report,
    const QStringList& activePids, const std::optional<QString>& selectedPid,
    double estimatedS);
}  // namespace px::modrim
