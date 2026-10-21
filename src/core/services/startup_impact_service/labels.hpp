#pragma once

#include <QString>

namespace px::modrim::startup_impact {

struct MetricKey {
  QString name;
  QString target;
};

// Splits a metric key into its name and "|Target" suffix, dropping the
// "LoadingProgress.StartupImpact." prefix the game writes before every key.
MetricKey splitMetricKey(const QString& key);

// Human-readable name for a metric key. A "|Target" suffix, as written for
// deferred tasks, is shown in parentheses after the label.
QString metricLabel(const QString& key);

}  // namespace px::modrim::startup_impact
