#pragma once

#include <QIcon>
#include <QString>
#include <QStringView>
#include <optional>

namespace px::modrim {

// The SVG markup of a named icon with `currentColor` replaced by `color`, or
// nullopt when no icon has that name.
std::optional<QString> iconSvg(QStringView name, QStringView color);

// Rasterised at >= 2x so small icons stay crisp on 1x screens; the pixmap's
// device pixel ratio carries the scale. Null icon for an unknown name.
QIcon icon(QStringView name, int size, QStringView color);

}  // namespace px::modrim
