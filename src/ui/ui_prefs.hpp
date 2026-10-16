#pragma once

#include <string>

#include "core/constants.hpp"

namespace px::modrim {

// Persisted UI state (ui_prefs.json). Field names are the JSON keys.
// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct UiPrefs {
  bool desc_expanded = false;
  LaunchStrategy launch_strategy = LaunchStrategy::kDirect;
  std::string skipped_update_tag;
  bool rail_collapsed = false;

  bool operator==(const UiPrefs&) const = default;
};
// NOLINTEND(readability-identifier-naming)

}  // namespace px::modrim
