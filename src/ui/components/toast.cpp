#include "ui/components/toast.hpp"

#include <QAbstractAnimation>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <cstddef>

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

// Every icon name used here is in the icon table; a miss is a programming
// error.
QIcon requireIcon(QStringView name, int size, QStringView color) {
  auto found = icon(name, size, color);
  Q_ASSERT(found.has_value());
  return found.value_or(QIcon());
}

}  // namespace

Toast::Toast(const QString& message, ToastLevel level, QWidget* parent)
    : QWidget(parent) {
  setObjectName(QStringLiteral("toast"));
  setFixedWidth(320);
  setAttribute(Qt::WA_StyledBackground, true);

  const LevelStyle& style = kLevelStyles[static_cast<std::size_t>(level)];

  m_opacityEffect = new QGraphicsOpacityEffect(this);
  m_opacityEffect->setOpacity(0.0);
  setGraphicsEffect(m_opacityEffect);

  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(12, 10, 8, 10);
  layout->setSpacing(8);

  auto* iconLabel = new QLabel(this);
  iconLabel->setPixmap(
      requireIcon(style.icon, 16, paletteValue(style.accent)).pixmap(16, 16));
  iconLabel->setFixedSize(16, 16);
  layout->addWidget(iconLabel);

  auto* messageLabel = new QLabel(message, this);
  messageLabel->setObjectName(QStringLiteral("toastMessage"));
  messageLabel->setWordWrap(true);
  layout->addWidget(messageLabel, 1);

  auto* close = new QPushButton(this);
  close->setObjectName(QStringLiteral("toastClose"));
  close->setIcon(requireIcon(u"close", 10, paletteValue(u"TEXT_DIM")));
  close->setFixedSize(20, 20);
  close->setToolTip(QStringLiteral("Dismiss"));
  connect(close, &QPushButton::clicked, this, &Toast::dismiss);
  layout->addWidget(close);

  m_opacityAnim = new QPropertyAnimation(m_opacityEffect,
                                         QByteArrayLiteral("opacity"), this);
  m_opacityAnim->setDuration(200);
  m_opacityAnim->setStartValue(0.0);
  m_opacityAnim->setEndValue(1.0);

  m_fadeOutAnim = new QPropertyAnimation(m_opacityEffect,
                                         QByteArrayLiteral("opacity"), this);
  m_fadeOutAnim->setDuration(150);
  m_fadeOutAnim->setEndValue(0.0);
  connect(m_fadeOutAnim, &QPropertyAnimation::finished, this,
          &Toast::dismissed);
}

void Toast::showWithAnimation() {
  show();
  raise();
  m_opacityAnim->setDirection(QAbstractAnimation::Direction::Forward);
  m_opacityAnim->start();
}

void Toast::dismiss() {
  if (m_dismissing) {
    return;
  }
  m_dismissing = true;
  m_opacityAnim->stop();
  m_fadeOutAnim->setStartValue(m_opacityEffect->opacity());
  m_fadeOutAnim->start();
}

ToastManager::ToastManager(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("toastManager"));
  setAttribute(Qt::WA_TransparentForMouseEvents, true);

  m_layout = new QVBoxLayout(this);
  m_layout->setContentsMargins(0, 0, 16, 16);
  m_layout->setSpacing(8);
  m_layout->setAlignment(Qt::AlignBottom | Qt::AlignRight);
}

void ToastManager::showToast(const QString& message, ToastLevel level,
                             int durationMs) {
  auto* toast = new Toast(message, level, this);
  m_layout->addWidget(toast);
  connect(toast, &Toast::dismissed, this, [this, toast] { remove(toast); });

  toast->showWithAnimation();

  if (durationMs > 0) {
    QTimer::singleShot(durationMs, toast, &Toast::dismiss);
  }
}

void ToastManager::info(const QString& message, int durationMs) {
  showToast(message, ToastLevel::kInfo, durationMs);
}

void ToastManager::success(const QString& message, int durationMs) {
  showToast(message, ToastLevel::kSuccess, durationMs);
}

void ToastManager::warning(const QString& message, int durationMs) {
  showToast(message, ToastLevel::kWarning, durationMs);
}

void ToastManager::error(const QString& message, int durationMs) {
  showToast(message, ToastLevel::kError, durationMs);
}

void ToastManager::resizeToParent() {
  if (const QWidget* parent = parentWidget()) {
    setGeometry(parent->rect());
  }
}

void ToastManager::remove(Toast* toast) {
  toast->hide();
  m_layout->removeWidget(toast);
  toast->deleteLater();
}

}  // namespace px::modrim
