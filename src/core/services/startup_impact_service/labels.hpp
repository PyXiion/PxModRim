#pragma once

#include <QString>

namespace px::modrim::startup_impact {

// Drops the "LoadingProgress.StartupImpact." prefix the game writes before
// every metric key.
QString stripMetricPrefix(const QString& key);

// Human-readable name for a metric key. A "|Target" suffix, as written for
// deferred tasks, is shown in parentheses after the label.
QString metricLabel(const QString& key);

}  // namespace px::modrim::startup_impact
