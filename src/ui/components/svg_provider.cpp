#include "ui/components/svg_provider.hpp"

#include <QPainter>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QSvgRenderer>
#include <QUrl>

#include "ui/components/icons.hpp"
#include "ui/theme/palette.hpp"

namespace px::modrim {

namespace {

constexpr QLatin1StringView kEmptySvg{R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"/>)"};

// The color parameter, percent-decoded. QML URL-encodes "#" and some paths
// deliver the digits without it, so a leading "23" is taken as an encoded "#".
QString colorFromQuery(const QString& query) {
    QString color = paletteValue(u"TEXT_MUTED");
    for (const QString& param : query.split(QLatin1Char('&'))) {
        if (!param.startsWith(QLatin1StringView("color="))) {
            continue;
        }
        color = QUrl::fromPercentEncoding(param.mid(6).toUtf8());
        if (!color.startsWith(QLatin1Char('#')) && color.startsWith(QLatin1StringView("23"))) {
            color = QStringLiteral("#") + color.mid(2);
        }
    }
    return color;
}

}  // namespace

SvgIconProvider::SvgIconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

QPixmap SvgIconProvider::requestPixmap(const QString& id, QSize*, const QSize& requestedSize) {
    const qsizetype questionMark = id.indexOf(QLatin1Char('?'));
    const QString name = questionMark < 0 ? id : id.left(questionMark);
    const QString query = questionMark < 0 ? QString{} : id.mid(questionMark + 1);

    const QString svg = iconSvg(name, colorFromQuery(query)).value_or(QString::fromLatin1(kEmptySvg));

    const QSize size = requestedSize.isValid() ? requestedSize : QSize(16, 16);
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    QSvgRenderer renderer(svg.toUtf8());
    renderer.render(&painter);
    painter.end();
    return pixmap;
}

QQmlEngine* createQmlEngine(QObject* parent) {
    if (QQuickStyle::name().isEmpty()) {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
    }
    auto* engine = new QQmlEngine(parent);
    // The engine takes ownership of image providers (it holds them in a shared
    // pointer), so the provider is created without a parent.
    engine->addImageProvider(QStringLiteral("icons"), new SvgIconProvider());
    return engine;
}

}  // namespace px::modrim
