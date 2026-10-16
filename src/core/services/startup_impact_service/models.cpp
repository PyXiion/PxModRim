#include "core/services/startup_impact_service/models.h"

#include <cmath>

namespace px::modrim::startup_impact {

namespace {

template <class Map>
double sum_values(const Map& map) {
    double total = 0.0;
    for (const double value : map) {
        total += value;
    }
    return total;
}

bool is_set(const std::optional<QString>& value) { return value && !value->isEmpty(); }

}  // namespace

double StartupImpactReport::total_time() const {
    double total = 0.0;
    for (const StartupImpactMod& mod : mods) {
        total += mod.total_impact_s;
    }
    return total;
}

double StartupImpactReport::off_thread_total_time() const {
    double total = 0.0;
    for (const StartupImpactMod& mod : mods) {
        total += mod.off_thread_total_impact_s;
    }
    return total;
}

double StartupImpactReport::mods_total_time() const {
    return total_time() + off_thread_total_time();
}

double StartupImpactReport::base_game_time() const { return sum_values(metrics); }

const StartupImpactMod* StartupImpactReport::find(const std::optional<QString>& package_id,
                                                  const std::optional<QString>& mod_name) const {
    if (is_set(package_id)) {
        const QString normalized = normalize_package_id(*package_id);
        for (const StartupImpactMod& mod : mods) {
            if (mod.package_id == normalized) {
                return &mod;
            }
        }
    }
    if (is_set(mod_name)) {
        const QString name_lower = mod_name->toLower();
        for (const StartupImpactMod& mod : mods) {
            if (mod.mod_name.toLower() == name_lower) {
                return &mod;
            }
        }
    }
    return nullptr;
}

QString normalize_package_id(QStringView package_id) {
    QString normalized = package_id.toString().toLower();
    constexpr QLatin1StringView steam_suffix{"_steam"};
    if (normalized.endsWith(steam_suffix)) {
        normalized.chop(steam_suffix.size());
    }
    return normalized;
}

QString format_impact(double seconds) {
    const auto ms = static_cast<long long>(std::nearbyint(seconds * 1000.0));
    return QStringLiteral("%1ms").arg(ms);
}

}  // namespace px::modrim::startup_impact
