#include "ui/components/icon_button.hpp"

#include <algorithm>

#include <QIcon>

#include "ui/components/icons.hpp"
#include "ui/theme/palette.hpp"

namespace px::modrim {

namespace {

// Every icon name used here is in the icon table; a miss is a programming error.
QIcon requireIcon(QStringView name, int size, QStringView color) {
    auto found = icon(name, size, color);
    Q_ASSERT(found.has_value());
    return found.value_or(QIcon());
}

}  // namespace

IconButton::IconButton(const QString& iconName, const QString& tooltip, bool primary, int size,
                       QWidget* parent)
    : QPushButton(parent), m_iconName(iconName), m_iconPx(std::max(16, size / 2)) {
    setIconColor(paletteValue(u"TEXT_MUTED"));
    setFixedSize(size, size);
    setObjectName(QStringLiteral("iconBtn"));
    // QSS min-height on QPushButton overrides setFixedSize; the 1px border sits outside the
    // QSS content box, so subtract it.
    const int inner = size - 2;
    setStyleSheet(QStringLiteral("QPushButton#iconBtn { min-width: %1px; max-width: %1px;"
                                 " min-height: %1px; max-height: %1px; }")
                      .arg(inner));
    if (primary) {
        setProperty("primary", true);
    }
    setCursor(Qt::PointingHandCursor);
    setToolTip(tooltip);
}

void IconButton::setIconColor(const QString& color) {
    setIcon(requireIcon(m_iconName, m_iconPx, color));
}

}  // namespace px::modrim
