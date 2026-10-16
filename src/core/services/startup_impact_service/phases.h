#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <QMap>
#include <QString>

namespace px::modrim::startup_impact {

enum class Phase : std::uint8_t {
    Content,
    ModConstructors,
    StaticConstructors,
    Patches,
    Defs,
    Deferred,
    Other,
};

inline constexpr std::size_t PHASE_COUNT = 7;

// Seconds per phase, indexed by `phase_index`.
using PhaseTotals = std::array<double, PHASE_COUNT>;

constexpr std::size_t phase_index(Phase phase) { return static_cast<std::size_t>(phase); }

QString phase_label(Phase phase);

// Phase a profiler metric key belongs to. Unknown keys fall into Other.
Phase metric_phase(const QString& key);

// Sums metric seconds per phase. Every phase is present, even when it has no metrics.
PhaseTotals phase_totals(const QMap<QString, double>& metrics);

}  // namespace px::modrim::startup_impact
