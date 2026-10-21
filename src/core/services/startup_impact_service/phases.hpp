#pragma once

#include <QMap>
#include <QString>
#include <array>
#include <cstddef>
#include <cstdint>

namespace px::modrim::startup_impact {

enum class Phase : std::uint8_t {
  kContent,
  kModConstructors,
  kStaticConstructors,
  kPatches,
  kDefs,
  kDeferred,
  kOther,
};

inline constexpr std::size_t kPhaseCount = 7;

// Seconds per phase, indexed by `phaseIndex`.
using PhaseTotals = std::array<double, kPhaseCount>;

constexpr std::size_t phaseIndex(Phase phase) {
  return static_cast<std::size_t>(phase);
}

QString phaseLabel(Phase phase);

// Phase a profiler metric key belongs to. Unknown keys fall into Other.
Phase metricPhase(const QString& key);

// Sums metric seconds per phase. Every phase is present, even when it has no
// metrics.
PhaseTotals phaseTotals(const QMap<QString, double>& metrics);

}  // namespace px::modrim::startup_impact
