#pragma once

#include <QLatin1StringView>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringView>
#include <optional>

namespace px::modrim::startup_impact {

inline constexpr double kImpactWarnThresholdS = 1.0;
inline constexpr double kImpactHighThresholdS = 5.0;
inline constexpr QLatin1StringView kBaseGamePid{"__base_game__"};

struct StartupImpactMod {
  QString modName;
  std::optional<QString> packageId;
  double totalImpactS = 0.0;
  QMap<QString, double> metrics;
  QMap<QString, double> offThreadMetrics;
  double offThreadTotalImpactS = 0.0;
};

struct StartupImpactReport {
  QString path;
  double loadingTimeS = 0.0;
  QList<StartupImpactMod> mods;
  QString timestamp;
  QMap<QString, double> metrics;
  double totalImpact = 0.0;
  QMap<QString, double> offThreadMetrics;
  double offThreadTotalImpact = 0.0;

  double totalTime() const;
  double offThreadTotalTime() const;
  double modsTotalTime() const;
  double baseGameTime() const;
  const StartupImpactMod* find(const std::optional<QString>& packageId,
                               const std::optional<QString>& modName) const;
};

QString normalizePackageId(QStringView packageId);
QString formatImpact(double seconds);

}  // namespace px::modrim::startup_impact
