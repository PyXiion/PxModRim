#include "core/services/startup_impact_service/models.hpp"

#include <cmath>

namespace px::modrim::startup_impact {

QString normalizePackageId(QStringView packageId) {
  QString result = packageId.toString().toLower();
  constexpr QLatin1StringView kSuffix{"_steam"};
  if (result.endsWith(kSuffix)) {
    result.chop(kSuffix.size());
  }
  return result;
}

QString formatImpact(double seconds) {
  const double milliseconds = seconds * 1000.0;
  const double magnitude = std::abs(milliseconds);
  double rounded = std::floor(magnitude);
  const double fraction = magnitude - rounded;
  // Choose the even neighbour at an exact tie, independent of the FP mode.
  if (fraction > 0.5 || (fraction == 0.5 && std::fmod(rounded, 2.0) != 0.0)) {
    rounded += 1.0;
  }
  if (milliseconds < 0.0 && rounded != 0.0) {
    rounded = -rounded;
  }
  return QString::number(rounded, 'f', 0) + QLatin1StringView{"ms"};
}

double StartupImpactReport::totalTime() const {
  double result = 0.0;
  for (const auto& mod : mods) {
    result += mod.totalImpactS;
  }
  return result;
}

double StartupImpactReport::offThreadTotalTime() const {
  double result = 0.0;
  for (const auto& mod : mods) {
    result += mod.offThreadTotalImpactS;
  }
  return result;
}

double StartupImpactReport::modsTotalTime() const {
  return totalTime() + offThreadTotalTime();
}

double StartupImpactReport::baseGameTime() const {
  double result = 0.0;
  for (double duration : metrics) {
    result += duration;
  }
  return result;
}

const StartupImpactMod* StartupImpactReport::find(
    const std::optional<QString>& packageId,
    const std::optional<QString>& modName) const {
  if (packageId && !packageId->isEmpty()) {
    const QString wanted = normalizePackageId(*packageId);
    for (const auto& mod : mods) {
      if (mod.packageId && *mod.packageId == wanted) {
        return &mod;
      }
    }
  }
  if (modName && !modName->isEmpty()) {
    const QString wanted = modName->toLower();
    for (const auto& mod : mods) {
      if (mod.modName.toLower() == wanted) {
        return &mod;
      }
    }
  }
  return nullptr;
}

}  // namespace px::modrim::startup_impact
