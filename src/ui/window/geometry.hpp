#pragma once

#include <QRect>
#include <algorithm>

namespace px::modrim {

// Clamps the size to at least 900x560 and to the screen less a 20px margin
// per side, but never beyond the screen, then moves the window on screen
// keeping up to that margin.
inline QRect fitWindowGeometry(const QRect& desired, const QRect& available) {
  static constexpr int kMargin = 20;
  const auto fitLength = [](int requested, int extent, int floor) {
    const int maximum = std::min(extent, std::max(floor, extent - 2 * kMargin));
    return std::clamp(requested, std::min(floor, maximum), maximum);
  };
  const auto fitPosition = [](int requested, int start, int extent,
                              int length) {
    const int margin = std::min(kMargin, (extent - length) / 2);
    return std::clamp(requested, start + margin,
                      start + extent - length - margin);
  };
  const int width = fitLength(desired.width(), available.width(), 900);
  const int height = fitLength(desired.height(), available.height(), 560);
  return {fitPosition(desired.x(), available.x(), available.width(), width),
          fitPosition(desired.y(), available.y(), available.height(), height),
          width, height};
}

}  // namespace px::modrim
