#pragma once

#include <QRect>
#include <algorithm>

namespace px::modrim {

inline QRect fitWindowGeometry(const QRect& desired, const QRect& available) {
  const auto dimension = [](int requested, int extent, int floor) {
    const int maximum = std::min(extent, std::max(floor, extent - 40));
    return std::clamp(requested, std::min(floor, maximum), maximum);
  };
  const QSize size(dimension(desired.width(), available.width(), 900),
                   dimension(desired.height(), available.height(), 560));
  const int marginX = std::min(20, (available.width() - size.width()) / 2);
  const int marginY = std::min(20, (available.height() - size.height()) / 2);
  const int left = available.x() + marginX;
  const int top = available.y() + marginY;
  const int right = available.x() + available.width() - size.width() - marginX;
  const int bottom =
      available.y() + available.height() - size.height() - marginY;
  return {std::clamp(desired.x(), left, right),
          std::clamp(desired.y(), top, bottom), size.width(), size.height()};
}

}  // namespace px::modrim
