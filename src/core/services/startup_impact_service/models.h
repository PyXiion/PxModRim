#pragma once

#include <optional>

#include <QLatin1StringView>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringView>

namespace px::modrim::startup_impact {

inline constexpr double IMPACT_WARN_THRESHOLD_S = 1.0;
inline constexpr double IMPACT_HIGH_THRESHOLD_S = 5.0;

// Stored as the mod's package id for the RimWorld base game's own profiler metrics.
inline constexpr QLatin1StringView BASE_GAME_PID{"__base_game__"};

// Impact durations are in seconds. Metric maps are keyed by profiler metric name and
// ordered by name, so iteration is deterministic.
struct StartupImpactMod {
    QString mod_name;
    std::optional<QString> package_id;
    double total_impact_s = 0.0;
    QMap<QString, double> metrics;
    QMap<QString, double> off_thread_metrics;
    double off_thread_total_impact_s = 0.0;
};

struct StartupImpactReport {
    QString path;
    double loading_time_s = 0.0;
    QList<StartupImpactMod> mods;
    QString timestamp;
    QMap<QString, double> metrics;
    double total_impact = 0.0;
    QMap<QString, double> off_thread_metrics;
    double off_thread_total_impact = 0.0;

    double total_time() const;
    double off_thread_total_time() const;
    double mods_total_time() const;
    double base_game_time() const;

    // Matches by normalized package id first, then by case-insensitive mod name.
    const StartupImpactMod* find(const std::optional<QString>& package_id,
                                 const std::optional<QString>& mod_name) const;
};

// Lowercases and drops the "_steam" suffix that Steam Workshop folders add to package ids.
QString normalize_package_id(QStringView package_id);

// Rounds half to even, as Python's round() does, so displayed values match the legacy app.
QString format_impact(double seconds);

}  // namespace px::modrim::startup_impact
