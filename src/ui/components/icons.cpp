#include "ui/components/icons.h"

#include <algorithm>

#include <QGuiApplication>
#include <QPainter>
#include <QRectF>
#include <QScreen>
#include <QSvgRenderer>
#include <QtGlobal>

namespace px::ui {

namespace {

struct IconEntry {
    QLatin1StringView name;
    QLatin1StringView svg;
};

constexpr IconEntry kIcons[]{
#include "ui/components/icon_table.inc"
};

constexpr QLatin1StringView kSvgNamespace{"xmlns=\"http://www.w3.org/2000/svg\""};

const IconEntry* findIcon(QStringView name) {
    for (const auto& entry : kIcons) {
        if (QString(entry.name) == name) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace

std::optional<QString> iconSvg(QStringView name, QStringView color) {
    const IconEntry* entry = findIcon(name);
    if (!entry) {
        return std::nullopt;
    }
    QString svg(entry->svg);
    if (!svg.contains(QString(kSvgNamespace))) {
        svg.replace(QStringLiteral("<svg "),
                    QStringLiteral("<svg ") + QString(kSvgNamespace) + QStringLiteral(" "));
    }
    svg.replace(QStringLiteral("currentColor"), color.toString());
    return svg;
}

std::optional<QPixmap> iconPixmap(QStringView name, int size, QStringView color) {
    const auto svg = iconSvg(name, color);
    if (!svg) {
        return std::nullopt;
    }

    const QScreen* screen = QGuiApplication::primaryScreen();
    const qreal scale = std::max<qreal>(2.0, screen ? screen->devicePixelRatio() : 1.0);
    const int pixels = qRound(size * scale);

    QPixmap pixmap(pixels, pixels);
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QSvgRenderer renderer(svg->toUtf8());
    if (renderer.isValid()) {
        renderer.render(&painter, QRectF(0, 0, size, size));
    }
    painter.end();
    return pixmap;
}

std::optional<QIcon> icon(QStringView name, int size, QStringView color) {
    auto pixmap = iconPixmap(name, size, color);
    if (!pixmap) {
        return std::nullopt;
    }
    return QIcon(*pixmap);
}

}  // namespace px::ui
