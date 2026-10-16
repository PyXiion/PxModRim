#include "ui/panels/startup_impact_data.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <tuple>
#include <QDateTime>
#include <QSet>
#include "core/services/startup_impact_service/labels.hpp"
#include "core/services/startup_impact_service/phases.hpp"
#include "ui/theme/palette.hpp"

namespace px::modrim {
using namespace startup_impact;
namespace {
constexpr double kMinSeconds = 0.001;
QString phaseColor(std::size_t i) {
    static constexpr std::array<const char*, 6> kColors{
        "#b57bee", "#f06ba8", "#43b581", "#f0883e", "#4fb3e8", "#e8c547"};
    return i < kColors.size() ? QString::fromLatin1(kColors[i]) : paletteValue(u"TEXT_DIM");
}
QString tooltip(const QMap<QString, double>& metrics) {
    QList<std::pair<QString, double>> sorted;
    for (auto it = metrics.cbegin(); it != metrics.cend(); ++it) sorted.append({it.key(), it.value()});
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    QStringList lines;
    for (qsizetype i = 0; i < std::min<qsizetype>(8, sorted.size()); ++i)
        if (sorted[i].second >= kMinSeconds) lines.append(sorted[i].first + ": " + formatDuration(sorted[i].second));
    if (sorted.size() > 8) lines.append(QStringLiteral("+ %1 more").arg(sorted.size() - 8));
    return lines.join('\n');
}
using Entry = std::tuple<QString, QString, bool, const QMap<QString, double>*>;
QVariantList phaseRows(const QList<Entry>& entries, const std::optional<QString>& selected) {
    using Contributor = std::tuple<double, QString, QString, bool>;
    std::array<QList<Contributor>, kPhaseCount> contributors;
    std::array<QMap<QString, double>, kPhaseCount> metrics;
    PhaseTotals sums{};
    for (const auto& [name, key, base, values] : entries) {
        const auto totals = phaseTotals(*values);
        for (std::size_t p = 0; p < kPhaseCount; ++p) {
            if (totals[p] < kMinSeconds) continue;
            contributors[p].append({totals[p], name, key, base});
            sums[p] += totals[p];
        }
        for (auto it = values->cbegin(); it != values->cend(); ++it)
            metrics[phaseIndex(metricPhase(it.key()))][metricLabel(it.key().section('|', 0, 0))] += it.value();
    }
    double grand = 0;
    for (double value : sums) grand += value;
    const double maximum = *std::max_element(sums.begin(), sums.end());
    std::array<std::size_t, kPhaseCount> order{0, 1, 2, 3, 4, 5, 6};
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return sums[a] > sums[b]; });
    QVariantList rows;
    for (auto p : order) {
        if (contributors[p].isEmpty()) continue;
        auto& items = contributors[p];
        std::sort(items.begin(), items.end(), std::greater<Contributor>{});
        QVariantList mods;
        const double top = std::get<0>(items.front());
        for (const auto& [seconds, name, key, base] : items)
            mods.append(QVariantMap{{"name", name}, {"key", key}, {"value", formatDuration(seconds)},
                {"fraction", seconds / top}, {"base", base}, {"selected", selected && key == *selected}});
        rows.append(QVariantMap{{"label", phaseLabel(static_cast<Phase>(p))}, {"color", phaseColor(p)},
            {"seconds", sums[p]}, {"value", formatDuration(sums[p])},
            {"share", grand > 0 ? QString::number(static_cast<int>(std::nearbyint(100 * sums[p] / grand))) + '%' : QString{}},
            {"fraction", sums[p] / maximum}, {"tooltip", tooltip(metrics[p])}, {"mods", mods}});
    }
    return rows;
}
}
QVariantMap buildStartupImpact(const StartupImpactReport& report, const QStringList& activePids,
                                const std::optional<QString>& selectedPid, double estimatedS) {
    QSet<QString> active;
    for (const auto& pid : activePids) active.insert(normalizePackageId(pid));
    const auto selected = selectedPid && !selectedPid->isEmpty()
        ? std::optional<QString>{normalizePackageId(*selectedPid)} : std::nullopt;
    const StartupImpactMod* base = nullptr;
    QList<const StartupImpactMod*> mods;
    double modsSeconds = 0, offSeconds = 0;
    for (const auto& mod : report.mods) {
        offSeconds += mod.offThreadTotalImpactS;
        if (mod.packageId && *mod.packageId == kBaseGamePid) { if (!base) base = &mod; }
        else { mods.append(&mod); modsSeconds += mod.totalImpactS; }
    }
    std::stable_sort(mods.begin(), mods.end(), [](auto a, auto b) { return a->totalImpactS > b->totalImpactS; });
    const double baseSeconds = base ? base->totalImpactS : 0;
    const double barTotal = baseSeconds + modsSeconds + offSeconds;
    const double maxMod = mods.isEmpty() ? 0 : mods.front()->totalImpactS;
    QVariantList rows, legend, summary;
    QList<Entry> entries;
    int activeCount = 0;
    QString selectedKey;
    for (qsizetype index = 0; index < mods.size(); ++index) {
        const auto& mod = *mods[index];
        const QString key = mod.packageId && !mod.packageId->isEmpty() ? *mod.packageId : mod.modName;
        const bool isActive = mod.packageId && active.contains(*mod.packageId);
        const bool isSelected = mod.packageId && selected && *mod.packageId == *selected;
        if (isActive) ++activeCount;
        if (isSelected && selectedKey.isEmpty()) selectedKey = key;
        const auto totals = phaseTotals(mod.metrics);
        std::array<QMap<QString, double>, kPhaseCount> grouped;
        for (auto it = mod.metrics.cbegin(); it != mod.metrics.cend(); ++it)
            grouped[phaseIndex(metricPhase(it.key()))][metricLabel(it.key())] += it.value();
        QList<std::size_t> order;
        QVariantList segments, phases;
        for (std::size_t p = 0; p < kPhaseCount; ++p) {
            if (totals[p] < kMinSeconds) continue;
            order.append(p);
            segments.append(QVariantMap{{"color", phaseColor(p)}, {"fraction", maxMod > 0 ? totals[p] / maxMod : 0}});
        }
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return totals[a] > totals[b]; });
        for (auto p : order)
            phases.append(QVariantMap{{"label", phaseLabel(static_cast<Phase>(p))}, {"color", phaseColor(p)},
                {"value", formatDuration(totals[p])}, {"fraction", totals[p] / totals[order.front()]}, {"tooltip", tooltip(grouped[p])}});
        rows.append(QVariantMap{{"key", key}, {"name", mod.modName}, {"rank", index + 1},
            {"seconds", mod.totalImpactS}, {"time", formatDuration(mod.totalImpactS)}, {"color", impactColor(mod.totalImpactS)},
            {"active", isActive}, {"selected", isSelected},
            {"off_thread", mod.offThreadTotalImpactS >= kMinSeconds ? formatDuration(mod.offThreadTotalImpactS) : QString{}},
            {"dominant", order.isEmpty() ? QString{} : phaseLabel(static_cast<Phase>(order.front()))}, {"segments", segments}, {"phases", phases}});
        entries.append({mod.modName, key, false, &mod.metrics});
    }
    for (std::size_t p = 0; p < kPhaseCount; ++p)
        legend.append(QVariantMap{{"label", phaseLabel(static_cast<Phase>(p))}, {"color", phaseColor(p)}});
    const std::array<QString, 3> labels{QStringLiteral("Base game"), QStringLiteral("Mods, main thread"), QStringLiteral("Off-thread (parallel)")};
    const std::array<double, 3> values{baseSeconds, modsSeconds, offSeconds};
    for (std::size_t i = 0; i < values.size(); ++i)
        if (values[i] >= kMinSeconds) summary.append(QVariantMap{{"label", labels[i]}, {"value", formatDuration(values[i])},
            {"fraction", barTotal > 0 ? values[i] / barTotal : 0}, {"color", paletteValue(i == 0 ? u"TEXT_DIM" : u"PRIMARY")}, {"parallel", i == 2}});
    const auto modsPhases = phaseRows(entries, selected);
    const QMap<QString, double> empty;
    entries.prepend({QStringLiteral("Base game"), QString(kBaseGamePid), true, base ? &base->metrics : &empty});
    auto timestamp = QDateTime::fromString(report.timestamp, Qt::ISODateWithMs);
    if (!timestamp.isValid()) timestamp = QDateTime::fromString(report.timestamp, Qt::ISODate);
    return {{"timestamp", timestamp.isValid() ? timestamp.toLocalTime().toString("yyyy-MM-dd HH:mm") : report.timestamp},
        {"last_launch", formatDuration(report.loadingTimeS != 0 ? report.loadingTimeS : baseSeconds + modsSeconds)},
        {"estimated", activePids.isEmpty() ? QString{} : formatDuration(estimatedS)}, {"summary", summary},
        {"base_value", formatDuration(baseSeconds)}, {"mods_value", formatDuration(modsSeconds)}, {"off_value", formatDuration(offSeconds)},
        {"legend", legend}, {"mods", rows}, {"active_count", activeCount}, {"selected_key", selectedKey},
        {"phases_with_base", phaseRows(entries, selected)}, {"phases_mods_only", modsPhases}};
}
} // namespace px::modrim
