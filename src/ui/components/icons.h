#pragma once

#include <optional>

#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QStringView>

namespace px::ui {

// The SVG markup of a named icon with `currentColor` replaced by `color`, or
// nullopt when no icon has that name.
std::optional<QString> iconSvg(QStringView name, QStringView color);

// Rasterised at >= 2x so small icons stay crisp on 1x screens; the pixmap's
// device pixel ratio carries the scale. nullopt for an unknown name.
std::optional<QPixmap> iconPixmap(QStringView name, int size, QStringView color);

std::optional<QIcon> icon(QStringView name, int size, QStringView color);

}  // namespace px::ui
