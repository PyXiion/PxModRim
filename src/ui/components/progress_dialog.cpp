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

// Every icon name used here is in the icon table; a miss is a programming error.
QIcon requireIcon(QStringView name, int size, QStringView color) {
    auto found = icon(name, size, color);
    Q_ASSERT(found.has_value());
    return found.value_or(QIcon());
}

}  // namespace

ProgressDialog::ProgressDialog(LoadingState& loading, QWidget* parent)
    : QDialog(parent, Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint | Qt::WindowTitleHint),
      loading_(loading) {
    setWindowTitle(QStringLiteral("Loading"));
    setMinimumWidth(420);
    setModal(true);

    crumbs_ = new QWidget(this);
    crumbs_layout_ = new QHBoxLayout(crumbs_);
    crumbs_layout_->setContentsMargins(0, 0, 0, 8);
    crumbs_layout_->setSpacing(4);

    progress_ = new QProgressBar(this);
    progress_->setTextVisible(false);
    progress_->setRange(0, 100);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(0);
    layout->addWidget(crumbs_);
    layout->addWidget(progress_);
    installDialogChrome(this, false);

    hide_timer_ = new QTimer(this);
    hide_timer_->setSingleShot(true);
    hide_timer_->setInterval(kHideDelayMs);
    connect(hide_timer_, &QTimer::timeout, this, &ProgressDialog::beginHide);

    fade_ = new QPropertyAnimation(this, QByteArrayLiteral("windowOpacity"), this);
    connect(fade_, &QPropertyAnimation::finished, this, &ProgressDialog::onFadeFinished);

    connections_.push_back(QObject::connect(
        &loading_, &LoadingState::changed, this,
        [this](const QString&, int pct, int, int) { onChanged(pct); }));
    connections_.push_back(
        QObject::connect(&loading_, &LoadingState::finished, this, &ProgressDialog::onFinished));
}

ProgressDialog::~ProgressDialog() {
    for (const auto& connection : connections_) {
        QObject::disconnect(connection);
    }
}

void ProgressDialog::onChanged(int pct) {
    hide_timer_->stop();
    if (!shown_) {
        shown_ = true;
        if (!isVisible()) {
            setWindowOpacity(0.0);
            show();
        }
        fadeTo(1.0, kFadeInMs);
    }

    const QStringList statuses = loading_.statuses();
    if (!statuses.isEmpty()) {
        setWindowTitle(statuses.first());
    }
    updateBreadcrumbs(statuses);
    progress_->setValue(pct);
}

void ProgressDialog::onFinished() {
    hide_timer_->start();
}

void ProgressDialog::updateBreadcrumbs(const QStringList& statuses) {
    while (QLayoutItem* item = crumbs_layout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    const qsizetype last = statuses.size() - 1;
    for (qsizetype i = 0; i < statuses.size(); ++i) {
        auto* label = new QLabel(statuses[i], crumbs_);
        if (i == last) {
            QFont font = label->font();
            font.setBold(true);
            label->setFont(font);
        }
        crumbs_layout_->addWidget(label);

        if (i < last) {
            auto* separator = new QLabel(crumbs_);
            separator->setPixmap(requireIcon(u"chevron", 12, paletteValue(u"TEXT_DIM")).pixmap(12, 12));
            crumbs_layout_->addWidget(separator);
        }
    }
    crumbs_layout_->addStretch();
}

void ProgressDialog::beginHide() {
    shown_ = false;
    fadeTo(0.0, kFadeOutMs);
}

void ProgressDialog::fadeTo(qreal end, int durationMs) {
    fade_->stop();
    fade_->setDuration(durationMs);
    fade_->setStartValue(windowOpacity());
    fade_->setEndValue(end);
    fade_->start();
}

void ProgressDialog::onFadeFinished() {
    if (!shown_) {
        hide();
    }
}

}  // namespace px::modrim
