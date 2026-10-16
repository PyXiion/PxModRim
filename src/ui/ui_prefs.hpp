#pragma once

#include <string>

#include "core/constants.hpp"

namespace px::modrim {

// Persisted UI state (ui_prefs.json). Field names are the JSON keys.
struct UiPrefs {
    bool desc_expanded = false;
    LaunchStrategy launch_strategy = LaunchStrategy::Direct;
    std::string skipped_update_tag;
    bool rail_collapsed = false;

    bool operator==(const UiPrefs&) const = default;
};

}  // namespace px::modrim
