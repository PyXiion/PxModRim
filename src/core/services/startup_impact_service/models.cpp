#include "core/services/startup_impact_service/models.hpp"

#include <cmath>

namespace px::modrim::startup_impact {

namespace {

template <class Map>
double sumValues(const Map& map) {
    double total = 0.0;
    for (const double value : map) {
        total += value;
    }
    return total;
}

bool isSet(const std::optional<QString>& value) { return value && !value->isEmpty(); }

}  // namespace

double StartupImpactReport::totalTime() const {
    double total = 0.0;
    for (const StartupImpactMod& mod : mods) {
        total += mod.totalImpactS;
    }
    return total;
}

double StartupImpactReport::offThreadTotalTime() const {
    double total = 0.0;
    for (const StartupImpactMod& mod : mods) {
        total += mod.offThreadTotalImpactS;
    }
    return total;
}

double StartupImpactReport::modsTotalTime() const {
    return totalTime() + offThreadTotalTime();
}

double StartupImpactReport::baseGameTime() const { return sumValues(metrics); }

const StartupImpactMod* StartupImpactReport::find(const std::optional<QString>& packageId,
                                                  const std::optional<QString>& modName) const {
    if (isSet(packageId)) {
        const QString normalized = normalizePackageId(*packageId);
        for (const StartupImpactMod& mod : mods) {
            if (mod.packageId == normalized) {
                return &mod;
            }
        }
    }
    if (isSet(modName)) {
        const QString nameLower = modName->toLower();
        for (const StartupImpactMod& mod : mods) {
            if (mod.modName.toLower() == nameLower) {
                return &mod;
            }
        }
    }
    return nullptr;
}

QString normalizePackageId(QStringView packageId) {
    QString normalized = packageId.toString().toLower();
    constexpr QLatin1StringView kSteamSuffix{"_steam"};
    if (normalized.endsWith(kSteamSuffix)) {
        normalized.chop(kSteamSuffix.size());
    }
    return normalized;
}

QString formatImpact(double seconds) {
    const auto ms = static_cast<long long>(std::nearbyint(seconds * 1000.0));
    return QStringLiteral("%1ms").arg(ms);
}

}  // namespace px::modrim::startup_impact
