#include "ui/components/banner_widget.h"

#include <algorithm>

#include <QColor>
#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSizePolicy>

#include "ui/theme/palette.h"

namespace px::ui {

namespace {

constexpr int kFallbackWidth = 300;
constexpr int kOverlayPad = 16;

// Floor division by two, so a negative offset rounds the way the legacy `//` did.
int floorHalf(int value) {
    return value >= 0 ? value / 2 : -((-value + 1) / 2);
}

}  // namespace

AspectRatioBanner::AspectRatioBanner(QWidget* parent, int maxHeight)
    : QWidget(parent), max_height_(maxHeight) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void AspectRatioBanner::setPixmap(const QPixmap& pixmap) {
    pixmap_ = pixmap;
    recomputeScaledPixmap();
    updateGeometry();
    update();
}

void AspectRatioBanner::setTitle(const QString& title) {
    title_ = title;
    update();
}

void AspectRatioBanner::setSubtitle(const QString& subtitle) {
    subtitle_ = subtitle;
    update();
}

void AspectRatioBanner::setShowOverlay(bool show) {
    show_overlay_ = show;
    update();
}

bool AspectRatioBanner::hasHeightForWidth() const {
    return true;
}

int AspectRatioBanner::heightForWidth(int w) const {
    return std::min(w * BANNER_ASPECT_H / BANNER_ASPECT_W, max_height_);
}

QSize AspectRatioBanner::sizeHint() const {
    const int w = width() > 0 ? width() : kFallbackWidth;
    return QSize(w, heightForWidth(w));
}

void AspectRatioBanner::recomputeScaledPixmap() {
    if (pixmap_.isNull() || width() <= 0) {
        scaled_pixmap_ = QPixmap();
        return;
    }

    scaled_pixmap_ = pixmap_.scaled(width(), heightForWidth(width()), Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation);
}

void AspectRatioBanner::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setClipRect(rect());

    if (scaled_pixmap_.isNull()) {
        painter.fillRect(rect(), panelBgQ());
    } else {
        const int x = floorHalf(width() - scaled_pixmap_.width());
        const int y = floorHalf(height() - scaled_pixmap_.height());
        painter.drawPixmap(x, y, scaled_pixmap_);
    }

    if (title_.isEmpty() || !show_overlay_) {
        return;
    }

    const int overlay_top = height() - BANNER_OVERLAY_HEIGHT;
    QLinearGradient gradient(0, overlay_top, 0, height());
    gradient.setColorAt(0.0, QColor(0, 0, 0, 0));
    gradient.setColorAt(1.0, QColor(0, 0, 0, 180));
    painter.fillRect(0, overlay_top, width(), BANNER_OVERLAY_HEIGHT, gradient);

    const int text_area_top = overlay_top + 12;
    const int text_area_w = width() - kOverlayPad * 2;

    QFont font = painter.font();
    font.setPointSize(18);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(textMainQ());

    int title_h = BANNER_OVERLAY_HEIGHT - 24;
    if (!subtitle_.isEmpty()) {
        title_h = title_h * 3 / 5;
    }
    painter.drawText(kOverlayPad, text_area_top, text_area_w, title_h,
                     Qt::AlignLeft | Qt::AlignBottom, title_);

    if (!subtitle_.isEmpty()) {
        font.setPointSize(12);
        font.setBold(false);
        painter.setFont(font);
        painter.setPen(textMutedQ());
        const int subtitle_top = text_area_top + title_h + 2;
        const int subtitle_h = BANNER_OVERLAY_HEIGHT - title_h - 26;
        painter.drawText(kOverlayPad, subtitle_top, text_area_w, subtitle_h,
                         Qt::AlignLeft | Qt::AlignTop, subtitle_);
    }
}

void AspectRatioBanner::resizeEvent(QResizeEvent* event) {
    recomputeScaledPixmap();
    QWidget::resizeEvent(event);
}

}  // namespace px::ui
