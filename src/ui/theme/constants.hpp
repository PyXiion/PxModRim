#pragma once

#include <QEasingCurve>

namespace px::modrim {

// Animation
inline constexpr int kAccordionAnimationDurationMs = 200;
inline constexpr QEasingCurve::Type kAccordionEasing = QEasingCurve::Type::OutCubic;

// Banner
inline constexpr int kBannerMaxHeight = 260;
inline constexpr int kBannerAspectW = 16;
inline constexpr int kBannerAspectH = 9;
inline constexpr int kBannerOverlayHeight = 120;

// Layout
inline constexpr int kRailMinWidth = 52;
inline constexpr int kRailMaxWidth = 180;
inline constexpr int kRailCollapseWidth = 116;
inline constexpr int kSidebarWidth = 240;

// Interaction
inline constexpr int kTooltipDelayMs = 400;

}  // namespace px::modrim
