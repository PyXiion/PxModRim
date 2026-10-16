#pragma once

#include <QEasingCurve>

namespace px::modrim {

// Animation
inline constexpr int ACCORDION_ANIMATION_DURATION_MS = 200;
inline constexpr QEasingCurve::Type ACCORDION_EASING = QEasingCurve::Type::OutCubic;

// Banner
inline constexpr int BANNER_MAX_HEIGHT = 260;
inline constexpr int BANNER_ASPECT_W = 16;
inline constexpr int BANNER_ASPECT_H = 9;
inline constexpr int BANNER_OVERLAY_HEIGHT = 120;

// Layout
inline constexpr int RAIL_MIN_WIDTH = 52;
inline constexpr int RAIL_MAX_WIDTH = 180;
inline constexpr int RAIL_COLLAPSE_WIDTH = 116;
inline constexpr int SIDEBAR_WIDTH = 240;

// Interaction
inline constexpr int TOOLTIP_DELAY_MS = 400;

}  // namespace px::modrim
