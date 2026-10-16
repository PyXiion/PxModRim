#include "ui/components/banner_widget.hpp"

#include <algorithm>

#include <QColor>
#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSizePolicy>

#include "ui/theme/palette.hpp"

namespace px::modrim {

namespace {

constexpr int kFallbackWidth = 300;
constexpr int kOverlayPad = 16;

// Floor division by two, so a negative offset rounds the way the legacy `//` did.
int floorHalf(int value) {
    return value >= 0 ? value / 2 : -((-value + 1) / 2);
}

}  // namespace

AspectRatioBanner::AspectRatioBanner(QWidget* parent, int maxHeight)
    : QWidget(parent), m_maxHeight(maxHeight) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void AspectRatioBanner::setPixmap(const QPixmap& pixmap) {
    m_pixmap = pixmap;
    recomputeScaledPixmap();
    updateGeometry();
    update();
}

void AspectRatioBanner::setTitle(const QString& title) {
    m_title = title;
    update();
}

void AspectRatioBanner::setSubtitle(const QString& subtitle) {
    m_subtitle = subtitle;
    update();
}

void AspectRatioBanner::setShowOverlay(bool show) {
    m_showOverlay = show;
    update();
}

bool AspectRatioBanner::hasHeightForWidth() const {
    return true;
}

int AspectRatioBanner::heightForWidth(int w) const {
    return std::min(w * kBannerAspectH / kBannerAspectW, m_maxHeight);
}

QSize AspectRatioBanner::sizeHint() const {
    const int w = width() > 0 ? width() : kFallbackWidth;
    return QSize(w, heightForWidth(w));
}

void AspectRatioBanner::recomputeScaledPixmap() {
    if (m_pixmap.isNull() || width() <= 0) {
        m_scaledPixmap = QPixmap();
        return;
    }

    m_scaledPixmap = m_pixmap.scaled(width(), heightForWidth(width()), Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation);
}

void AspectRatioBanner::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setClipRect(rect());

    if (m_scaledPixmap.isNull()) {
        painter.fillRect(rect(), panelBgQ());
    } else {
        const int x = floorHalf(width() - m_scaledPixmap.width());
        const int y = floorHalf(height() - m_scaledPixmap.height());
        painter.drawPixmap(x, y, m_scaledPixmap);
    }

    if (m_title.isEmpty() || !m_showOverlay) {
        return;
    }

    const int overlayTop = height() - kBannerOverlayHeight;
    QLinearGradient gradient(0, overlayTop, 0, height());
    gradient.setColorAt(0.0, QColor(0, 0, 0, 0));
    gradient.setColorAt(1.0, QColor(0, 0, 0, 180));
    painter.fillRect(0, overlayTop, width(), kBannerOverlayHeight, gradient);

    const int textAreaTop = overlayTop + 12;
    const int textAreaW = width() - kOverlayPad * 2;

    QFont font = painter.font();
    font.setPointSize(18);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(textMainQ());

    int titleH = kBannerOverlayHeight - 24;
    if (!m_subtitle.isEmpty()) {
        titleH = titleH * 3 / 5;
    }
    painter.drawText(kOverlayPad, textAreaTop, textAreaW, titleH,
                     Qt::AlignLeft | Qt::AlignBottom, m_title);

    if (!m_subtitle.isEmpty()) {
        font.setPointSize(12);
        font.setBold(false);
        painter.setFont(font);
        painter.setPen(textMutedQ());
        const int subtitleTop = textAreaTop + titleH + 2;
        const int subtitleH = kBannerOverlayHeight - titleH - 26;
        painter.drawText(kOverlayPad, subtitleTop, textAreaW, subtitleH,
                         Qt::AlignLeft | Qt::AlignTop, m_subtitle);
    }
}

void AspectRatioBanner::resizeEvent(QResizeEvent* event) {
    recomputeScaledPixmap();
    QWidget::resizeEvent(event);
}

}  // namespace px::modrim
