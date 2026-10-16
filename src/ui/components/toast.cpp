#include "ui/components/toast.hpp"

#include <cstddef>

#include <QAbstractAnimation>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "ui/components/icons.hpp"
#include "ui/theme/palette.hpp"

namespace px::modrim {

namespace {

struct LevelStyle {
    QStringView icon;
    QStringView accent;
};

// Indexed by ToastLevel.
constexpr LevelStyle kLevelStyles[]{
    {u"info", u"PRIMARY"},
    {u"check-circle", u"SUCCESS"},
    {u"warning", u"WARNING"},
    {u"error", u"DANGER"},
};

// Every icon name used here is in the icon table; a miss is a programming error.
QIcon requireIcon(QStringView name, int size, QStringView color) {
    auto found = icon(name, size, color);
    Q_ASSERT(found.has_value());
    return found.value_or(QIcon());
}

}  // namespace

Toast::Toast(const QString& message, ToastLevel level, QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("toast"));
    setFixedWidth(320);
    setAttribute(Qt::WA_StyledBackground, true);

    const LevelStyle& style = kLevelStyles[static_cast<std::size_t>(level)];

    opacity_effect_ = new QGraphicsOpacityEffect(this);
    opacity_effect_->setOpacity(0.0);
    setGraphicsEffect(opacity_effect_);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 10, 8, 10);
    layout->setSpacing(8);

    auto* icon_label = new QLabel(this);
    icon_label->setPixmap(requireIcon(style.icon, 16, paletteValue(style.accent)).pixmap(16, 16));
    icon_label->setFixedSize(16, 16);
    layout->addWidget(icon_label);

    auto* message_label = new QLabel(message, this);
    message_label->setObjectName(QStringLiteral("toastMessage"));
    message_label->setWordWrap(true);
    layout->addWidget(message_label, 1);

    auto* close = new QPushButton(this);
    close->setObjectName(QStringLiteral("toastClose"));
    close->setIcon(requireIcon(u"close", 10, paletteValue(u"TEXT_DIM")));
    close->setFixedSize(20, 20);
    close->setToolTip(QStringLiteral("Dismiss"));
    connect(close, &QPushButton::clicked, this, &Toast::dismiss);
    layout->addWidget(close);

    opacity_anim_ = new QPropertyAnimation(opacity_effect_, QByteArrayLiteral("opacity"), this);
    opacity_anim_->setDuration(200);
    opacity_anim_->setStartValue(0.0);
    opacity_anim_->setEndValue(1.0);

    fade_out_anim_ = new QPropertyAnimation(opacity_effect_, QByteArrayLiteral("opacity"), this);
    fade_out_anim_->setDuration(150);
    fade_out_anim_->setEndValue(0.0);
    connect(fade_out_anim_, &QPropertyAnimation::finished, this, &Toast::dismissed);
}

void Toast::showWithAnimation() {
    show();
    raise();
    opacity_anim_->setDirection(QAbstractAnimation::Direction::Forward);
    opacity_anim_->start();
}

void Toast::dismiss() {
    if (dismissing_) {
        return;
    }
    dismissing_ = true;
    opacity_anim_->stop();
    fade_out_anim_->setStartValue(opacity_effect_->opacity());
    fade_out_anim_->start();
}

ToastManager::ToastManager(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("toastManager"));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 16, 16);
    layout_->setSpacing(8);
    layout_->setAlignment(Qt::AlignBottom | Qt::AlignRight);
}

void ToastManager::showToast(const QString& message, ToastLevel level, int durationMs) {
    auto* toast = new Toast(message, level, this);
    layout_->addWidget(toast);
    connect(toast, &Toast::dismissed, this, [this, toast] { remove(toast); });

    toast->showWithAnimation();

    if (durationMs > 0) {
        QTimer::singleShot(durationMs, toast, &Toast::dismiss);
    }
}

void ToastManager::info(const QString& message, int durationMs) {
    showToast(message, ToastLevel::Info, durationMs);
}

void ToastManager::success(const QString& message, int durationMs) {
    showToast(message, ToastLevel::Success, durationMs);
}

void ToastManager::warning(const QString& message, int durationMs) {
    showToast(message, ToastLevel::Warning, durationMs);
}

void ToastManager::error(const QString& message, int durationMs) {
    showToast(message, ToastLevel::Error, durationMs);
}

void ToastManager::resizeToParent() {
    if (const QWidget* parent = parentWidget()) {
        setGeometry(parent->rect());
    }
}

void ToastManager::remove(Toast* toast) {
    toast->hide();
    layout_->removeWidget(toast);
    toast->deleteLater();
}

}  // namespace px::modrim
