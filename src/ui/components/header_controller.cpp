#include "ui/components/header_controller.h"

#include <initializer_list>
#include <utility>

#include "core/constants.h"

namespace px::ui {

namespace {

QString strategyLabel(core::LaunchStrategy strategy) {
    switch (strategy) {
        case core::LaunchStrategy::Direct:
            return QStringLiteral("Direct executable");
        case core::LaunchStrategy::Steam:
            return QStringLiteral("Steam");
    }
    return {};
}

}  // namespace

HeaderController::HeaderController(bool isFrameless, int initialStrategy, QString appVersion,
                                   QVariantMap tooltips, bool downloadsAvailable, QObject* parent)
    : QObject(parent),
      is_frameless_(isFrameless),
      app_version_(std::move(appVersion)),
      tooltips_(std::move(tooltips)),
      downloads_available_(downloadsAvailable),
      strategy_index_(initialStrategy) {}

QVariantList HeaderController::strategies() const {
    QVariantList list;
    for (const auto strategy : {core::LaunchStrategy::Direct, core::LaunchStrategy::Steam}) {
        list.append(QVariantMap{{QStringLiteral("index"), static_cast<int>(strategy)},
                                {QStringLiteral("label"), strategyLabel(strategy)}});
    }
    return list;
}

void HeaderController::setMaximized(bool value) {
    if (maximized_ != value) {
        maximized_ = value;
        emit maximizedChanged();
    }
}

void HeaderController::setUnsavedChanges(bool value) {
    if (unsaved_changes_ != value) {
        unsaved_changes_ = value;
        emit unsavedChangesChanged();
    }
}

void HeaderController::setLaunchState(const QString& state) {
    if (launch_state_ != state) {
        launch_state_ = state;
        emit launchStateChanged();
    }
}

void HeaderController::setTaskText(const QString& text) {
    if (task_text_ != text) {
        task_text_ = text;
        emit taskTextChanged();
    }
}

void HeaderController::setDownloadsBusy(bool busy) {
    if (downloads_busy_ != busy) {
        downloads_busy_ = busy;
        emit downloadsBusyChanged();
    }
}

void HeaderController::setDownloadsProgressShown(bool shown) {
    if (downloads_progress_shown_ != shown) {
        downloads_progress_shown_ = shown;
        emit downloadsProgressShownChanged();
    }
}

void HeaderController::setDownloadsProgress(const QString& text, int value, int total) {
    downloads_text_ = text;
    downloads_value_ = value;
    downloads_total_ = total;
    emit downloadsProgressChanged();
}

void HeaderController::refresh() { emit refreshRequested(); }

void HeaderController::autoSort() { emit sortRequested(); }

void HeaderController::openDownloads() { emit downloadsRequested(); }

void HeaderController::updateMods() { emit updateModsRequested(); }

void HeaderController::save() { emit saveRequested(); }

void HeaderController::launch() { emit launchRequested(); }

void HeaderController::minimize() { emit minimizeRequested(); }

void HeaderController::maximize() { emit maximizeRequested(); }

void HeaderController::closeWindow() { emit closeRequested(); }

void HeaderController::setStrategy(int index) {
    if (strategy_index_ != index) {
        strategy_index_ = index;
        emit strategyChanged(index);
    }
}

void HeaderController::dragStarted() { emit dragStartRequested(); }

}  // namespace px::ui
