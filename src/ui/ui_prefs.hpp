#pragma once

#include <QByteArray>
#include <algorithm>
#include <cmath>
#include <string>

#include <glaze/glaze.hpp>

#include "core/constants.hpp"

namespace px::modrim {

inline constexpr int clampUiScale(int percent) {
  return std::clamp(percent, 100, 250);
}

inline int parseUiScale(const glz::json_t& value) {
  if (!value.is_number()) return 100;
  const double percent = value.get<double>();
  if (!std::isfinite(percent) || std::trunc(percent) != percent) return 100;
  return static_cast<int>(std::clamp(percent, 100.0, 250.0));
}

inline QByteArray uiScaleFactor(int percent) {
  return QByteArray::number(clampUiScale(percent) / 100.0, 'g', 3);
}

// Persisted UI state (ui_prefs.json). Field names are the JSON keys.
// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct UiPrefs {
  bool desc_expanded = false;
  LaunchStrategy launch_strategy = LaunchStrategy::kDirect;
  std::string skipped_update_tag;
  bool rail_collapsed = false;
  int ui_scale = 100;

  bool operator==(const UiPrefs&) const = default;
};
// NOLINTEND(readability-identifier-naming)

}  // namespace px::modrim
