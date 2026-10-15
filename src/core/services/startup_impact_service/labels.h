#pragma once

#include <QString>

namespace px::core::startup_impact {

// Drops the "LoadingProgress.StartupImpact." prefix the game writes before every metric key.
QString strip_metric_prefix(const QString& key);

// Human-readable name for a metric key. A "|Target" suffix, as written for deferred tasks,
// is shown in parentheses after the label.
QString metric_label(const QString& key);

}  // namespace px::core::startup_impact
