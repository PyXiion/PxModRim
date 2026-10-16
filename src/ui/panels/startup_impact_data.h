#pragma once

#include <optional>
#include <QStringList>
#include <QVariantMap>
#include "core/services/startup_impact_service/models.h"
#include "ui/models/impact.h"

namespace px::modrim {
QVariantMap build_startup_impact(const startup_impact::StartupImpactReport& report,
                                const QStringList& active_pids,
                                const std::optional<QString>& selected_pid, double estimated_s);
} // namespace px::modrim
