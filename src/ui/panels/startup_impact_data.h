#pragma once

#include <optional>
#include <QStringList>
#include <QVariantMap>
#include "core/services/startup_impact_service/models.h"
#include "ui/models/impact.h"

namespace px::ui {
QVariantMap build_startup_impact(const core::startup_impact::StartupImpactReport& report,
                                const QStringList& active_pids,
                                const std::optional<QString>& selected_pid, double estimated_s);
} // namespace px::ui
