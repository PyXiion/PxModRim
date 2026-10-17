#include "ui/components/progress_dialog.hpp"

#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayoutItem>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>

#include "ui/components/dialog_chrome.hpp"
#include "ui/components/icons.hpp"
#include "ui/theme/palette.hpp"

namespace px::modrim {

namespace {

constexpr int kHideDelayMs = 300;
constexpr int kFadeInMs = 150;
constexpr int kFadeOutMs = 200;

// Every icon name used here is in the icon table; a miss is a programming
// error.
QIcon requireIcon(QStringView name, int size, QStringView color) {
  auto found = icon(name, size, color);
  Q_ASSERT(found.has_value());
  return found.value_or(QIcon());
}

}  // namespace

ProgressDialog::ProgressDialog(LoadingState& loading, QWidget* parent)
    : QDialog(parent, Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint |
                          Qt::WindowTitleHint),
      m_loading(loading) {
  setWindowTitle(QStringLiteral("Loading"));
  setMinimumWidth(420);
  setModal(true);

  m_crumbs = new QWidget(this);
  m_crumbsLayout = new QHBoxLayout(m_crumbs);
  m_crumbsLayout->setContentsMargins(0, 0, 0, 8);
  m_crumbsLayout->setSpacing(4);

  m_progress = new QProgressBar(this);
  m_progress->setTextVisible(false);
  m_progress->setRange(0, 100);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(0);
  layout->addWidget(m_crumbs);
  layout->addWidget(m_progress);
  installDialogChrome(this, false);

  m_hideTimer = new QTimer(this);
  m_hideTimer->setSingleShot(true);
  m_hideTimer->setInterval(kHideDelayMs);
  connect(m_hideTimer, &QTimer::timeout, this, &ProgressDialog::beginHide);

  m_fade =
      new QPropertyAnimation(this, QByteArrayLiteral("windowOpacity"), this);
  connect(m_fade, &QPropertyAnimation::finished, this,
          &ProgressDialog::onFadeFinished);

  m_connections.push_back(QObject::connect(
      &m_loading, &LoadingState::changed, this,
      [this](const QString&, int pct, int, int) { onChanged(pct); }));
  m_connections.push_back(QObject::connect(&m_loading, &LoadingState::finished,
                                           this, &ProgressDialog::onFinished));
}

ProgressDialog::~ProgressDialog() {
  for (const auto& connection : m_connections) {
    QObject::disconnect(connection);
  }
}

void ProgressDialog::onChanged(int pct) {
  m_hideTimer->stop();
  if (!m_shown) {
    m_shown = true;
    if (!isVisible()) {
      setWindowOpacity(0.0);
      show();
    }
    fadeTo(1.0, kFadeInMs);
  }

  const QStringList statuses = m_loading.statuses();
  if (!statuses.isEmpty()) {
    setWindowTitle(statuses.first());
  }
  updateBreadcrumbs(statuses);
  m_progress->setValue(pct);
}

void ProgressDialog::onFinished() { m_hideTimer->start(); }

void ProgressDialog::updateBreadcrumbs(const QStringList& statuses) {
  while (QLayoutItem* item = m_crumbsLayout->takeAt(0)) {
    if (QWidget* widget = item->widget()) {
      widget->hide();
      widget->deleteLater();
    }
    delete item;
  }

  const qsizetype last = statuses.size() - 1;
  for (qsizetype i = 0; i < statuses.size(); ++i) {
    auto* label = new QLabel(statuses[i], m_crumbs);
    if (i == last) {
      QFont font = label->font();
      font.setBold(true);
      label->setFont(font);
    }
    m_crumbsLayout->addWidget(label);

    if (i < last) {
      auto* separator = new QLabel(m_crumbs);
      separator->setPixmap(
          requireIcon(u"chevron", 12, paletteValue(u"TEXT_DIM"))
              .pixmap(12, 12));
      m_crumbsLayout->addWidget(separator);
    }
  }
  m_crumbsLayout->addStretch();
}

void ProgressDialog::beginHide() {
  m_shown = false;
  fadeTo(0.0, kFadeOutMs);
}

void ProgressDialog::fadeTo(qreal end, int durationMs) {
  m_fade->stop();
  m_fade->setDuration(durationMs);
  m_fade->setStartValue(windowOpacity());
  m_fade->setEndValue(end);
  m_fade->start();
}

void ProgressDialog::onFadeFinished() {
  if (!m_shown) {
    hide();
  }
}

}  // namespace px::modrim
