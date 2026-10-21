#include "core/services/startup_impact_service/models.hpp"

namespace px::modrim::startup_impact {

QString normalizePackageId(QStringView packageId) {
  QString result = packageId.toString().toLower();
  constexpr QLatin1StringView kSuffix{"_steam"};
  if (result.endsWith(kSuffix)) {
    result.chop(kSuffix.size());
  }
  return result;
}

double StartupImpactReport::baseGameTime() const {
  double result = 0.0;
  for (double duration : metrics) {
    result += duration;
  }
  return result;
}

const StartupImpactMod* StartupImpactReport::find(
    const std::optional<QString>& packageId) const {
  if (packageId && !packageId->isEmpty()) {
    const QString wanted = normalizePackageId(*packageId);
    for (const auto& mod : mods) {
      if (mod.packageId && *mod.packageId == wanted) {
        return &mod;
      }
    }
  }
  return nullptr;
}

}  // namespace px::modrim::startup_impact
