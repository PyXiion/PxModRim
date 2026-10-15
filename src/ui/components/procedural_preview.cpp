#include "ui/components/procedural_preview.h"
#include "ui/theme/constants.h"
#include <QCache>
#include <QCryptographicHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QRadialGradient>
#include <QRegularExpression>
#include <QStringList>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace px::ui {
QString initials(const QString& title) {
    static const QRegularExpression tags(R"([\[(][^\])]*[\])])");
    static const QRegularExpression words(R"([^\W_]+)", QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression camel(R"([A-Z]+(?![a-z])|[A-Z]?[a-z]+|\d+|[^\W\d_a-zA-Z]+)", QRegularExpression::UseUnicodePropertiesOption);
    static const QSet<QString> stop{"a", "an", "and", "for", "of", "the", "to", "in", "on"};
    QString stripped = title;
    stripped.replace(tags, " ");
    const bool use_stripped = words.match(stripped).hasMatch();
    QStringList all, meaningful;
    auto chunks = words.globalMatch(use_stripped ? stripped : title);
    while (chunks.hasNext()) {
        const QString chunk = chunks.next().captured();
        auto parts = camel.globalMatch(chunk);
        if (!parts.hasNext()) all.append(chunk);
        while (parts.hasNext()) all.append(parts.next().captured());
    }
    for (const auto& word : all) if (!stop.contains(word.toLower())) meaningful.append(word);
    if (meaningful.isEmpty()) meaningful = all;
    if (meaningful.isEmpty()) return title.trimmed().isEmpty() ? QStringLiteral("?") : title.trimmed().left(2).toUpper();
    if (meaningful.size() == 1) {
        auto tag_matches = tags.globalMatch(title);
        QString tag_text;
        while (tag_matches.hasNext()) tag_text += tag_matches.next().captured() + " ";
        const auto tag_word = words.match(tag_text);
        if (use_stripped && tag_word.hasMatch()) return (tag_word.captured().left(1) + meaningful.front().left(1)).toUpper();
        return meaningful.front().left(2).toUpper();
    }
    return (meaningful[0].left(1) + meaningful[1].left(1)).toUpper();
}
namespace {
double seedrand(const QString& seed) {
    const auto hash = QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha256);
    unsigned remainder = 0;
    for (char byte : hash) remainder = (remainder * 256 + static_cast<unsigned char>(byte)) % 10000;
    return remainder / 10000.0;
}
QColor hsl(double hue, double saturation, double lightness) {
    return QColor::fromHslF(hue - std::floor(hue), std::clamp(saturation, 0.0, 1.0), std::clamp(lightness, 0.0, 1.0));
}
void blob(QPainter& painter, double x, double y, double radius, const QColor& color) {
    QRadialGradient gradient(QPointF(x, y), radius);
    QColor outer = color;
    outer.setAlpha(0);
    gradient.setColorAt(0, color);
    gradient.setColorAt(1, outer);
    painter.setBrush(gradient);
    painter.drawEllipse(QPointF(x, y), radius, radius);
}
}
QImage generate_preview(const QString& title, int width, int height) {
    static QMutex mutex;
    static QCache<QString, QImage> cache(16);
    const QString key = QString::number(width) + ":" + QString::number(height) + ":" + title;
    {
        QMutexLocker lock(&mutex);
        if (auto* cached = cache.object(key)) return *cached;
    }
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    const double hue = seedrand(title);
    image.fill(hsl(hue, .40, .12));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const int span = std::max(width, height);
    const double jx = seedrand(title + "jx") * .2, jy = seedrand(title + "jy") * .2;
    QColor warm = hsl(hue, .7, .5), cool = hsl(hue + .14, .7, .5);
    warm.setAlphaF(.75); cool.setAlphaF(.65);
    blob(painter, width * (.15 + jx), height * (.1 + jy), span * .6, warm);
    blob(painter, width * (.85 - jx), height * (.9 - jy), span * .55, cool);
    QFont font = painter.font();
    font.setBold(true); font.setWeight(QFont::ExtraBold);
    font.setPixelSize(std::max(32, static_cast<int>(height * .36)));
    font.setLetterSpacing(QFont::PercentageSpacing, 96);
    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255, 210));
    const int visible = std::max(height - BANNER_OVERLAY_HEIGHT, height / 2);
    painter.drawText(QRectF(0, 0, width, visible), Qt::AlignCenter, initials(title));
    painter.end();
    QMutexLocker lock(&mutex);
    cache.insert(key, new QImage(image));
    return image;
}
}
