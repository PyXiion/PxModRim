#pragma once

#include <QColor>
#include <QString>
#include <QStringView>

class QApplication;

namespace px::modrim {

// Look up a color token by its uppercase name, e.g. "ELEVATE_0", "PRIMARY".
// Returns an empty string if the name is unknown.
QString paletteValue(QStringView name);

// Same as paletteValue, parsed into a QColor.
QColor paletteColor(QStringView name);

// Pre-constructed QColors matching legacy panel_bg_q, text_main_q, text_muted_q.
const QColor& panelBgQ();
const QColor& textMainQ();
const QColor& textMutedQ();

// Returns the application stylesheet (style.qss with $KEY tokens expanded).
QString getStylesheet();

// Sets Fusion style, dark palette, and the parsed stylesheet on `app`,
// matching legacy pxmodrim/_app.py `_apply_theme`.
void applyAppTheme(QApplication& app);

}  // namespace px::modrim
