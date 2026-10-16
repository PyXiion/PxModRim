#include "ui/panels/startup_impact_data.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <tuple>
#include <QDateTime>
#include <QSet>
#include "core/services/startup_impact_service/labels.h"
#include "core/services/startup_impact_service/phases.h"
#include "ui/theme/palette.h"

namespace px::modrim {
using namespace startup_impact;
namespace {
constexpr double minSeconds = 0.001;
QString phaseColor(std::size_t i) {
    static constexpr std::array<const char*, 6> colors{
        "#b57bee", "#f06ba8", "#43b581", "#f0883e", "#4fb3e8", "#e8c547"};
    return i < colors.size() ? QString::fromLatin1(colors[i]) : paletteValue(u"TEXT_DIM");
}
QString tooltip(const QMap<QString, double>& metrics) {
    QList<std::pair<QString, double>> sorted;
    for (auto it = metrics.cbegin(); it != metrics.cend(); ++it) sorted.append({it.key(), it.value()});
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    QStringList lines;
    for (qsizetype i = 0; i < std::min<qsizetype>(8, sorted.size()); ++i)
        if (sorted[i].second >= minSeconds) lines.append(sorted[i].first + ": " + format_duration(sorted[i].second));
    if (sorted.size() > 8) lines.append(QStringLiteral("+ %1 more").arg(sorted.size() - 8));
    return lines.join('\n');
}
using Entry = std::tuple<QString, QString, bool, const QMap<QString, double>*>;
QVariantList phaseRows(const QList<Entry>& entries, const std::optional<QString>& selected) {
    using Contributor = std::tuple<double, QString, QString, bool>;
    std::array<QList<Contributor>, PHASE_COUNT> contributors;
    std::array<QMap<QString, double>, PHASE_COUNT> metrics;
    PhaseTotals sums{};
    for (const auto& [name, key, base, values] : entries) {
        const auto totals = phase_totals(*values);
        for (std::size_t p = 0; p < PHASE_COUNT; ++p) {
            if (totals[p] < minSeconds) continue;
            contributors[p].append({totals[p], name, key, base});
            sums[p] += totals[p];
        }
        for (auto it = values->cbegin(); it != values->cend(); ++it)
            metrics[phase_index(metric_phase(it.key()))][metric_label(it.key().section('|', 0, 0))] += it.value();
    }
    double grand = 0;
    for (double value : sums) grand += value;
    const double maximum = *std::max_element(sums.begin(), sums.end());
    std::array<std::size_t, PHASE_COUNT> order{0, 1, 2, 3, 4, 5, 6};
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return sums[a] > sums[b]; });
    QVariantList rows;
    for (auto p : order) {
        if (contributors[p].isEmpty()) continue;
        auto& items = contributors[p];
        std::sort(items.begin(), items.end(), std::greater<Contributor>{});
        QVariantList mods;
        const double top = std::get<0>(items.front());
        for (const auto& [seconds, name, key, base] : items)
            mods.append(QVariantMap{{"name", name}, {"key", key}, {"value", format_duration(seconds)},
                {"fraction", seconds / top}, {"base", base}, {"selected", selected && key == *selected}});
        rows.append(QVariantMap{{"label", phase_label(static_cast<Phase>(p))}, {"color", phaseColor(p)},
            {"seconds", sums[p]}, {"value", format_duration(sums[p])},
            {"share", grand > 0 ? QString::number(static_cast<int>(std::nearbyint(100 * sums[p] / grand))) + '%' : QString{}},
            {"fraction", sums[p] / maximum}, {"tooltip", tooltip(metrics[p])}, {"mods", mods}});
    }
    return rows;
}
}
QVariantMap build_startup_impact(const StartupImpactReport& report, const QStringList& active_pids,
                                const std::optional<QString>& selected_pid, double estimated_s) {
    QSet<QString> active;
    for (const auto& pid : active_pids) active.insert(normalize_package_id(pid));
    const auto selected = selected_pid && !selected_pid->isEmpty()
        ? std::optional<QString>{normalize_package_id(*selected_pid)} : std::nullopt;
    const StartupImpactMod* base = nullptr;
    QList<const StartupImpactMod*> mods;
    double modsSeconds = 0, offSeconds = 0;
    for (const auto& mod : report.mods) {
        offSeconds += mod.off_thread_total_impact_s;
        if (mod.package_id && *mod.package_id == BASE_GAME_PID) { if (!base) base = &mod; }
        else { mods.append(&mod); modsSeconds += mod.total_impact_s; }
    }
    std::stable_sort(mods.begin(), mods.end(), [](auto a, auto b) { return a->total_impact_s > b->total_impact_s; });
    const double baseSeconds = base ? base->total_impact_s : 0;
    const double barTotal = baseSeconds + modsSeconds + offSeconds;
    const double maxMod = mods.isEmpty() ? 0 : mods.front()->total_impact_s;
    QVariantList rows, legend, summary;
    QList<Entry> entries;
    int activeCount = 0;
    QString selectedKey;
    for (qsizetype index = 0; index < mods.size(); ++index) {
        const auto& mod = *mods[index];
        const QString key = mod.package_id && !mod.package_id->isEmpty() ? *mod.package_id : mod.mod_name;
        const bool isActive = mod.package_id && active.contains(*mod.package_id);
        const bool isSelected = mod.package_id && selected && *mod.package_id == *selected;
        if (isActive) ++activeCount;
        if (isSelected && selectedKey.isEmpty()) selectedKey = key;
        const auto totals = phase_totals(mod.metrics);
        std::array<QMap<QString, double>, PHASE_COUNT> grouped;
        for (auto it = mod.metrics.cbegin(); it != mod.metrics.cend(); ++it)
            grouped[phase_index(metric_phase(it.key()))][metric_label(it.key())] += it.value();
        QList<std::size_t> order;
        QVariantList segments, phases;
        for (std::size_t p = 0; p < PHASE_COUNT; ++p) {
            if (totals[p] < minSeconds) continue;
            order.append(p);
            segments.append(QVariantMap{{"color", phaseColor(p)}, {"fraction", maxMod > 0 ? totals[p] / maxMod : 0}});
        }
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return totals[a] > totals[b]; });
        for (auto p : order)
            phases.append(QVariantMap{{"label", phase_label(static_cast<Phase>(p))}, {"color", phaseColor(p)},
                {"value", format_duration(totals[p])}, {"fraction", totals[p] / totals[order.front()]}, {"tooltip", tooltip(grouped[p])}});
        rows.append(QVariantMap{{"key", key}, {"name", mod.mod_name}, {"rank", index + 1},
            {"seconds", mod.total_impact_s}, {"time", format_duration(mod.total_impact_s)}, {"color", impact_color(mod.total_impact_s)},
            {"active", isActive}, {"selected", isSelected},
            {"off_thread", mod.off_thread_total_impact_s >= minSeconds ? format_duration(mod.off_thread_total_impact_s) : QString{}},
            {"dominant", order.isEmpty() ? QString{} : phase_label(static_cast<Phase>(order.front()))}, {"segments", segments}, {"phases", phases}});
        entries.append({mod.mod_name, key, false, &mod.metrics});
    }
    for (std::size_t p = 0; p < PHASE_COUNT; ++p)
        legend.append(QVariantMap{{"label", phase_label(static_cast<Phase>(p))}, {"color", phaseColor(p)}});
    const std::array<QString, 3> labels{QStringLiteral("Base game"), QStringLiteral("Mods, main thread"), QStringLiteral("Off-thread (parallel)")};
    const std::array<double, 3> values{baseSeconds, modsSeconds, offSeconds};
    for (std::size_t i = 0; i < values.size(); ++i)
        if (values[i] >= minSeconds) summary.append(QVariantMap{{"label", labels[i]}, {"value", format_duration(values[i])},
            {"fraction", barTotal > 0 ? values[i] / barTotal : 0}, {"color", paletteValue(i == 0 ? u"TEXT_DIM" : u"PRIMARY")}, {"parallel", i == 2}});
    const auto modsPhases = phaseRows(entries, selected);
    const QMap<QString, double> empty;
    entries.prepend({QStringLiteral("Base game"), QString(BASE_GAME_PID), true, base ? &base->metrics : &empty});
    auto timestamp = QDateTime::fromString(report.timestamp, Qt::ISODateWithMs);
    if (!timestamp.isValid()) timestamp = QDateTime::fromString(report.timestamp, Qt::ISODate);
    return {{"timestamp", timestamp.isValid() ? timestamp.toLocalTime().toString("yyyy-MM-dd HH:mm") : report.timestamp},
        {"last_launch", format_duration(report.loading_time_s != 0 ? report.loading_time_s : baseSeconds + modsSeconds)},
        {"estimated", active_pids.isEmpty() ? QString{} : format_duration(estimated_s)}, {"summary", summary},
        {"base_value", format_duration(baseSeconds)}, {"mods_value", format_duration(modsSeconds)}, {"off_value", format_duration(offSeconds)},
        {"legend", legend}, {"mods", rows}, {"active_count", activeCount}, {"selected_key", selectedKey},
        {"phases_with_base", phaseRows(entries, selected)}, {"phases_mods_only", modsPhases}};
}
} // namespace px::modrim
