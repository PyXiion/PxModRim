#include "ui/window/main_window.hpp"

#include <QCoro/QCoroFuture>
#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QIcon>
#include <QKeySequence>
#include <QFileInfo>
#include <QPointer>
#include <QKeyEvent>
#include <QLineEdit>
#include <QQmlContext>
#include <QQmlEngine>
#include <QResizeEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantMap>
#include <QWindow>
#include <QtConcurrent/QtConcurrentRun>

#include "core/downloads/manager.hpp"
#include "core/mod_service.hpp"
#include "core/services/diagnostics_service.hpp"
#include "core/services/game_launcher.hpp"
#include "core/version.hpp"
#include "ui/components/dialogs.hpp"
#include "ui/components/header_controller.hpp"
#include "ui/components/header_panel.hpp"
#include "ui/components/mod_updates.hpp"
#include "ui/components/svg_provider.hpp"
#include "ui/components/toast.hpp"
#include "ui/components/view_rail_panel.hpp"
#include "ui/config.hpp"
#include "ui/mod_selection.hpp"
#include "ui/navigation.hpp"
#include "ui/panels/about_panel.hpp"
#include "ui/panels/keyboard_shortcuts_dialog.hpp"
#include "ui/panels/mod_info_panel.hpp"
#include "ui/panels/mod_list_panel.hpp"
#include "ui/panels/restore_snapshot_dialog.hpp"
#include "ui/panels/settings_panel.hpp"
#include "ui/panels/update_dialog.hpp"
#include "ui/panels/upload_report_dialog.hpp"
#include "ui/theme/constants.hpp"
#include "ui/theme/qml_theme.hpp"
#include "ui/views/mods_view.hpp"
#include "ui/window/menu_bar.hpp"

namespace px::modrim {

UnsavedChangesDialog::UnsavedChangesDialog(QWidget* parent) : QMessageBox(parent) {
    setIcon(Question);
    setWindowTitle(QStringLiteral("Unsaved Changes"));
    setText(QStringLiteral("The active mod list has unsaved changes."));
    setInformativeText(QStringLiteral("Save before closing, discard the changes, or cancel closing."));
    setStandardButtons(Save | Discard | Cancel);
    setDefaultButton(Save);
}

LaunchConfirmDialog::LaunchConfirmDialog(QWidget* parent, const QString& title,
        const QString& text, const QString& info, const QString& accept) : QMessageBox(parent) {
    setIcon(Warning);
    setWindowTitle(title);
    setText(text);
    setInformativeText(info);
    setStandardButtons(Ok | Cancel);
    button(Ok)->setText(accept);
    setDefaultButton(Cancel);
    setCheckBox(new QCheckBox(QStringLiteral("Don't ask again"), this));
}

bool LaunchConfirmDialog::dontAskAgain() const { return checkBox()->isChecked(); }

MainWindow::MainWindow(AppContext& app, QWidget* parent)
    : QMainWindow(parent), app_(app), ctx_(app.coreContext()),
      engine_(createQmlEngine(this)), saved_active_(ctx_.loadedActiveUuids()),
      updates_(appVersion(), ctx_.network()) {
    setWindowTitle(QStringLiteral("PxModRim[*]"));
    setWindowIcon(QIcon(QStringLiteral(":/qt/qml/PxModRim/assets/logo.svg")));
    resize(1400, 850);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint
                   | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
    setFocusPolicy(Qt::ClickFocus);
    engine_->rootContext()->setContextProperty(QStringLiteral("Theme"), new Theme(engine_));

    const QVariantMap tooltips{
        {"refresh", actionSpec(ActionId::Refresh).tooltip()},
        {"sort", actionSpec(ActionId::AutoSort).tooltip()},
        {"save", actionSpec(ActionId::Save).tooltip()},
        {"update_mods", actionSpec(ActionId::UpdateMods).tooltip()},
    };
    header_controller_ = new HeaderController(windowFlags().testFlag(Qt::FramelessWindowHint),
        static_cast<int>(app_.uiPrefs().launch_strategy), appVersion(), tooltips,
        ctx_.downloads().available(), this);
    header_ = new HeaderPanel(*header_controller_, engine_, this);
    setupActions();
    menu_ = new MenuBar(actions_, this);
    menu_->hide();
    connect(header_controller_, &HeaderController::refreshRequested, this, [this] { reloadMods(); });
    connect(header_controller_, &HeaderController::sortRequested, this, [this] { autoSort(); });
    connect(header_controller_, &HeaderController::saveRequested, this, [this] { saveActiveMods(); });
    connect(header_controller_, &HeaderController::launchRequested, this, [this] { launchGame(); });
    connect(header_controller_, &HeaderController::updateModsRequested, this, [this] { updateMods(); });
    connect(header_controller_, &HeaderController::downloadsRequested, this, &MainWindow::openDownloads);
    connect(header_controller_, &HeaderController::minimizeRequested, this, &QWidget::showMinimized);
    connect(header_controller_, &HeaderController::maximizeRequested, this, [this] {
        isMaximized() ? showNormal() : showMaximized();
    });
    connect(header_controller_, &HeaderController::closeRequested, this, &QWidget::close);
    connect(header_controller_, &HeaderController::dragStartRequested, this, [this] {
        if (windowHandle()) windowHandle()->startSystemMove();
    });
    connect(header_controller_, &HeaderController::strategyChanged, this, [this](int index) {
        const auto strategy = static_cast<LaunchStrategy>(index);
        if (app_.uiPrefs().launch_strategy != strategy) {
            app_.uiPrefs().launch_strategy = strategy;
            savePrefs();
        }
    });

    auto* outer = new QWidget(this);
    outer->setObjectName(QStringLiteral("outerContainer"));
    auto* layout = new QVBoxLayout(outer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addWidget(menu_);
    QVariantList tabs;
    for (const auto& view : app_.railViews()) {
        tabs.append(QVariantMap{{"viewId", QString(view.meta.viewId)},
                               {"icon", QString(view.meta.iconName)}, {"label", QString(view.meta.label)}});
    }
    QVariantList help;
    for (const auto id : {ActionId::ReportIssue, ActionId::UploadLogs, ActionId::OpenLogs,
                          ActionId::Shortcuts, ActionId::CheckUpdates, ActionId::About}) {
        const auto& spec = actionSpec(id);
        help.append(QVariantMap{{"id", spec.key}, {"label", spec.label}});
    }
    splitter_ = new QSplitter(Qt::Horizontal, outer);
    splitter_->setContentsMargins(0, 0, 0, 0);
    splitter_->setHandleWidth(1);
    rail_ = new ViewRailPanel(tabs, help, engine_, splitter_);
    rail_->setObjectName(QStringLiteral("viewRail"));
    rail_->setMinimumWidth(RAIL_MIN_WIDTH);
    rail_->setMaximumWidth(RAIL_MAX_WIDTH);
    stack_ = new QStackedWidget(splitter_);
    stack_->setObjectName(QStringLiteral("viewStack"));
    splitter_->addWidget(rail_);
    splitter_->addWidget(stack_);
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setCollapsible(0, false);
    splitter_->setCollapsible(1, false);
    const int railWidth = app_.uiPrefs().rail_collapsed ? RAIL_MIN_WIDTH : RAIL_MAX_WIDTH;
    splitter_->setSizes({railWidth, width() - railWidth});
    connect(rail_, &ViewRailPanel::currentChanged, this, &MainWindow::selectView);
    connect(rail_, &ViewRailPanel::hovered, this, &MainWindow::preloadView);
    connect(rail_, &ViewRailPanel::settingsRequested, this, [this] { app_.navigate(buildRoute(QString(kSettingsViewId))); });
    connect(rail_, &ViewRailPanel::helpActionRequested, this, [this](const QString& key) {
        for (const auto& spec : actionSpecs()) if (spec.key == key) actions_.at(spec.id)->trigger();
    });
    connect(splitter_, &QSplitter::splitterMoved, this, [this] { snapRail(); });
    for (const auto& spec : app_.railViews()) {
        auto* view = spec.create(ctx_, engine_, stack_, app_);
        views_.push_back(view);
        stack_->addWidget(view);
        if (spec.meta.viewId == QLatin1StringView("mods")) mods_view_ = qobject_cast<ModsViewPanel*>(view);
    }
    if (mods_view_) {
        selection_ = std::make_unique<ModSelectionPresenter>(ctx_, *mods_view_->mod_info);
        connect(mods_view_, &ModsViewPanel::entry_selected, this, [this](const view::SidebarEntry& entry) {
            mods_view_->apply_sidebar_entry(entry);
        });
        connect(mods_view_, &ModsViewPanel::mod_selected, this, [this](const QString& uuid) { selectMod(uuid); });
        connect(mods_view_->mod_info, &ModInfoPanel::modRequested, this, [this](const QString& uuid) { requestMod(uuid); });
    }
    layout->addWidget(splitter_, 1);
    setCentralWidget(outer);
    toasts_ = new ToastManager(outer);
    toasts_->resizeToParent();
    app_.setToasts(toasts_);
    app_.setNavigator([this](const QString& url) { openRoute(url); });
    active_connection_ = ctx_.activeStateChanged().connect([this](const auto&) { refreshUnsavedState(); });
    mods_connection_ = ctx_.modService().mods_changed.connect([this](const auto&) { onModsReloaded(); });
    auto& diagnostics = ctx_.diagnosticsService();
    diagnostics_connection_ = diagnostics.diagnostics_summary_changed.connect([this](const auto&) {
        if (!mods_view_) return;
        mods_view_->apply_current_sidebar_filter();
        if (!selected_uuid_.isEmpty()) mods_view_->mod_info->setIssues(ctx_.diagnosticsService().issues_for(selected_uuid_));
    });
    status_connection_ = diagnostics.status_message_changed.connect([this](const QString& message) { toasts_->info(message); });
    task_connection_ = diagnostics.background_task_changed.connect([this](const QString& text) { header_controller_->setTaskText(text); });
    auto& downloads = ctx_.downloads();
    if (downloads.available()) {
        download_busy_connection_ = downloads.busy_changed.connect([this](bool busy) {
            header_controller_->setDownloadsBusy(busy);
            actions_.at(ActionId::UpdateMods)->setEnabled(!busy);
            if (busy) header_controller_->setDownloadsProgress(QStringLiteral("Updating mods…"), 0, 0);
        });
        download_progress_connection_ = downloads.download_progress.connect([this](const DownloadProgress& progress) {
            header_controller_->setDownloadsProgress(
                QStringLiteral("Updating mods: %1 / %2 · %3 MB").arg(progress.completed).arg(progress.total)
                    .arg(progress.bytes_done / 1048576.0, 0, 'f', 0), progress.completed, progress.total);
        });
        download_finished_connection_ = downloads.download_finished.connect([this](const auto& result) { onDownloadsFinished(result); });
        download_status_connection_ = downloads.status_message_changed.connect([this](const QString& message) { toasts_->info(message); });
    }
    qApp->installEventFilter(this);
    if (!views_.empty()) showView(0);
    refreshUnsavedState();
}

MainWindow::~MainWindow() {
    qApp->removeEventFilter(this);
    app_.setNavigator({});
    app_.setToasts(nullptr);
    ctx_.activeStateChanged().disconnect(active_connection_);
    ctx_.modService().mods_changed.disconnect(mods_connection_);
    auto& diagnostics = ctx_.diagnosticsService();
    diagnostics.diagnostics_summary_changed.disconnect(diagnostics_connection_);
    diagnostics.status_message_changed.disconnect(status_connection_);
    diagnostics.background_task_changed.disconnect(task_connection_);
    auto& downloads = ctx_.downloads();
    downloads.busy_changed.disconnect(download_busy_connection_);
    downloads.download_progress.disconnect(download_progress_connection_);
    downloads.download_finished.disconnect(download_finished_connection_);
    downloads.status_message_changed.disconnect(download_status_connection_);
    const auto dialogs = findChildren<QDialog*>(QString(), Qt::FindDirectChildrenOnly);
    for (auto* dialog : dialogs) delete dialog;
    // QuickWidgets must release their scenes before the shared engine is destroyed.
    delete takeCentralWidget();
    delete engine_;
}

void MainWindow::setupActions() {
    actions_ = createActions(*this);
    const auto bind = [this](ActionId id, auto handler) { connect(actions_.at(id), &QAction::triggered, this, handler); };
    bind(ActionId::Save, [this] { saveActiveMods(); });
    bind(ActionId::Restore, [this] { restoreSnapshot(); });
    bind(ActionId::Settings, [this] { app_.navigate(buildRoute(QString(kSettingsViewId))); });
    bind(ActionId::Quit, [this] { close(); });
    bind(ActionId::Refresh, [this] { reloadMods(); });
    bind(ActionId::FullRescan, [this] { reloadMods(true); });
    bind(ActionId::AutoSort, [this] { autoSort(); });
    bind(ActionId::UpdateMods, [this] { updateMods(); });
    bind(ActionId::FocusSearch, [this] { focusSearch(); });
    bind(ActionId::NextView, [this] { cycleView(1); });
    bind(ActionId::PrevView, [this] { cycleView(-1); });
    bind(ActionId::Fullscreen, [this] { isFullScreen() ? showNormal() : showFullScreen(); });
    bind(ActionId::ReportIssue, [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/PyXiion/PxModRim/issues"))); });
    bind(ActionId::OpenLogs, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(config_dir()).filePath(QStringLiteral("logs")))); });
    bind(ActionId::UploadLogs, [this] { uploadReport(); });
    bind(ActionId::Shortcuts, [this] { showShortcuts(); });
    bind(ActionId::CheckUpdates, [this] { checkUpdates(true); });
    bind(ActionId::About, [this] { showAbout(); });
    actions_.at(ActionId::UpdateMods)->setEnabled(ctx_.downloads().available());
    for (int index = 0; index < 9; ++index) {
        auto* action = new QAction(this);
        action->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(index + 1)));
        connect(action, &QAction::triggered, this, [this, index] { selectView(index); });
        addAction(action);
    }
}

void MainWindow::setAppQuitCallback(std::function<void()> callback) { quit_ = std::move(callback); }

void MainWindow::openRoute(const QString& url) {
    const auto route = parseRoute(url);
    if (!route) return;
    if (route->view_id == kSettingsViewId) { openSettings(); return; }
    const auto& specs = app_.railViews();
    for (std::size_t i = 0; i < views_.size(); ++i) {
        if (route->view_id == specs[i].meta.viewId) {
            showView(static_cast<int>(i));
            views_[i]->openRoute(route->path);
            return;
        }
    }
}

void MainWindow::selectView(int index) {
    if (index >= 0 && index < stack_->count()) app_.navigate(buildRoute(QString(app_.railViews()[index].meta.viewId)));
}

void MainWindow::showView(int index) {
    rail_->setCurrent(index);
    stack_->setCurrentIndex(index);
    header_controller_->setDownloadsProgressShown(!app_.railViews()[index].meta.listsDownloads);
    preloadView(index - 1);
    preloadView(index + 1);
}

void MainWindow::preloadView(int index) {
    if (index >= 0 && index < static_cast<int>(views_.size())) views_[index]->preload();
}

void MainWindow::cycleView(int direction) {
    const int count = stack_->count();
    if (count) selectView(((stack_->currentIndex() + direction) % count + count) % count);
}

void MainWindow::openDownloads() {
    for (const auto& view : app_.railViews()) {
        if (view.meta.listsDownloads) { app_.navigate(buildRoute(QString(view.meta.viewId))); return; }
    }
}

void MainWindow::focusSearch() {
    auto* search = stack_->currentWidget() ? stack_->currentWidget()->findChild<QLineEdit*>(QStringLiteral("searchInput")) : nullptr;
    if (search) search->setFocus();
    else if (mods_view_) mods_view_->mod_list->search_input->setFocus();
}

void MainWindow::savePrefs() {
    if (ctx_.hasConfigService()) {
        const auto result = saveUiPrefs(app_.uiPrefs(), ctx_.configService());
        if (!result) toasts_->error(result.error().message);
    }
}

void MainWindow::snapRail() {
    const int target = rail_->width() >= RAIL_COLLAPSE_WIDTH ? RAIL_MAX_WIDTH : RAIL_MIN_WIDTH;
    splitter_->setSizes({target, splitter_->width() - target});
    const bool collapsed = target == RAIL_MIN_WIDTH;
    if (app_.uiPrefs().rail_collapsed != collapsed) { app_.uiPrefs().rail_collapsed = collapsed; savePrefs(); }
}

void MainWindow::refreshUnsavedState() {
    unsaved_ = ctx_.activeUuids() != saved_active_;
    header_controller_->setUnsavedChanges(unsaved_);
    setWindowModified(unsaved_);
}

void MainWindow::onModsReloaded() { saved_active_ = ctx_.loadedActiveUuids(); refreshUnsavedState(); }

bool MainWindow::eventFilter(QObject* object, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        auto* widget = qobject_cast<QWidget*>(object);
        if (widget && widget->window() == this) {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Alt) {
                menu_->setVisible(!menu_->isVisible());
                if (menu_->isVisible()) menu_->setFocus();
                return true;
            }
            if (key == Qt::Key_Escape && menu_->isVisible()) { menu_->hide(); return true; }
        }
    }
    return QMainWindow::eventFilter(object, event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (unsaved_ && !close_confirmed_) {
        event->ignore();
        if (!close_prompt_open_) { close_prompt_open_ = true; confirmClose(); }
        return;
    }
    closing_ = true;
    for (auto* view : views_) view->teardown();
    event->accept();
    if (quit_) quit_();
}

QCoro::Task<void> MainWindow::confirmClose() {
    const auto [result, dialog] = co_await awaitDialog<UnsavedChangesDialog>(this);
    close_prompt_open_ = false;
    if (result == QMessageBox::Save) {
        if (!co_await saveActiveMods()) co_return;
    } else if (result != QMessageBox::Discard) co_return;
    close_confirmed_ = true;
    close();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (toasts_) toasts_->resizeToParent();
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && header_controller_) header_controller_->setMaximized(isMaximized());
}

QCoro::Task<void> MainWindow::reloadMods(bool full) {
    toasts_->info(full ? QStringLiteral("Rescanning all mods…") : QStringLiteral("Refreshing mods…"));
    const auto count = co_await app_.refreshMods(full);
    if (!count) { toasts_->error(count.error().message); co_return; }
    if (!*count) { toasts_->warning(QStringLiteral("No mods found")); co_return; }
    toasts_->success((full ? QStringLiteral("Rescanned %1 mods") : QStringLiteral("Reloaded %1 mods")).arg(*count));
}

QCoro::Task<void> MainWindow::autoSort() {
    const auto [count, elapsed] = co_await ctx_.autoSort();
    toasts_->success(QStringLiteral("Sorted %1 mods in %2ms").arg(count).arg(elapsed, 0, 'f', 0));
}

QCoro::Task<void> MainWindow::updateMods() {
    auto& downloads = ctx_.downloads();
    if (!downloads.available()) co_return;
    if (downloads.is_downloading()) { downloads.cancel(); co_return; }
    const auto ids = downloads.updatable_ids(ctx_.allMods().values());
    if (ids.isEmpty()) { toasts_->info(QStringLiteral("No downloaded mods to update")); co_return; }
    if (!co_await confirm_update(ids.size(), this)) co_return;
    toasts_->info(QStringLiteral("Updating %1 mods…").arg(ids.size()));
    const auto result = co_await downloads.download_mods(ids);
    if (!result) toasts_->error(result.error().message);
}

void MainWindow::onDownloadsFinished(const DownloadResult& result) {
    const auto ok = result.succeeded.size(), failed = result.failed.size(), changed = result.changed.size();
    if (!ok && !failed) { toasts_->info(QStringLiteral("Download cancelled")); return; }
    const QString summary = QStringLiteral("%1 updated, %2 unchanged").arg(changed).arg(ok - changed);
    if (failed) {
        const auto names = ctx_.downloads().names_by_id(ctx_.allMods().values());
        QStringList shown;
        for (const auto& id : result.failed.mid(0, 3)) shown.append(names.value(id, id));
        toasts_->warning(QStringLiteral("%1, %2 failed: %3%4").arg(summary).arg(failed).arg(shown.join(QStringLiteral(", ")), failed > 3 ? QStringLiteral("…") : QString()));
    } else toasts_->success(summary);
    if (changed) app_.refreshMods();
}

QCoro::Task<void> MainWindow::openSettings() {
    if (settings_open_) co_return;
    settings_open_ = true;
    const auto [result, dialog] = co_await awaitDialog<SettingsPanel>(app_, engine_, this);
    settings_open_ = false;
    if (result != QDialog::Accepted) co_return;
    auto config = dialog->getConfig();
    const auto saved = ctx_.configService().save(QStringLiteral("config.json"), config);
    if (!saved) { toasts_->error(saved.error().message); co_return; }
    ctx_.updateConfig(config);
    ctx_.resetProviders(config.paths);
    const auto reloaded = co_await ctx_.modService().reload();
    if (!reloaded) { toasts_->error(reloaded.error().message); co_return; }
    toasts_->success(QStringLiteral("Settings saved"));
}

QCoro::Task<void> MainWindow::showAbout() { co_await awaitDialog<AboutPanel>(this, ctx_, *toasts_); }
QCoro::Task<void> MainWindow::showShortcuts() { co_await awaitDialog<KeyboardShortcutsDialog>(shortcutRows() + QML_SHORTCUTS, this); }
QCoro::Task<void> MainWindow::uploadReport() { co_await handle_upload_report(this, ctx_, *toasts_); }

QCoro::Task<void> MainWindow::restoreSnapshot() {
    const auto snapshots = ctx_.modService().get_snapshots();
    if (snapshots.isEmpty()) { toasts_->warning(QStringLiteral("No saved mod lists are available")); co_return; }
    const auto [result, dialog] = co_await awaitDialog<RestoreSnapshotDialog>(snapshots, this);
    if (result != QDialog::Accepted) co_return;
    const auto chosen = dialog->selectedSnapshot();
    if (!chosen) co_return;
    const auto [confirmed, confirmation] = co_await awaitDialog<ConfirmRestoreDialog>(QFileInfo(*chosen).fileName(), this);
    if (confirmed != QMessageBox::Yes) co_return;
    if (co_await ctx_.modService().restore_snapshot(*chosen)) toasts_->success(QStringLiteral("Restored mod list from %1").arg(QFileInfo(*chosen).fileName()));
    else toasts_->error(QStringLiteral("The saved mod list is invalid or cannot be read"));
}

void MainWindow::startStartupUpdateCheck() { checkUpdates(false); }

QCoro::Task<void> MainWindow::checkUpdates(bool manual) {
    if (checking_updates_) co_return;
    checking_updates_ = true;
    const QPointer<MainWindow> guard(this);
    const auto release = co_await updates_.check();
    if (!guard || closing_) co_return;
    if (!release) {
        checking_updates_ = false;
        if (manual) toasts_->error(QStringLiteral("Could not check for updates"));
        co_return;
    }
    if (!release->has_value()) {
        checking_updates_ = false;
        if (manual) toasts_->success(QStringLiteral("PxModRim is up to date"));
        co_return;
    }
    const auto info = **release;
    if (!manual && info.tag.toStdString() == app_.uiPrefs().skipped_update_tag) { checking_updates_ = false; co_return; }
    const auto [result, dialog] = co_await awaitDialog<UpdateDialog>(info, updates_.current_version(), this);
    if (!guard || closing_) co_return;
    checking_updates_ = false;
    if (result == QDialog::Accepted) QDesktopServices::openUrl(QUrl(info.url));
    else if (result == SKIP_RESULT) { app_.uiPrefs().skipped_update_tag = info.tag.toStdString(); savePrefs(); }
}

QCoro::Task<std::optional<QStringList>> MainWindow::writeActiveLayout() {
    const auto active = ctx_.activeUuids();
    if (!co_await ctx_.modService().save_active_layout(active)) co_return std::nullopt;
    saved_active_ = active;
    refreshUnsavedState();
    co_return active;
}

QCoro::Task<bool> MainWindow::saveActiveMods() {
    const auto saved = co_await writeActiveLayout();
    if (!saved) { toasts_->warning(QStringLiteral("Config folder not set")); co_return false; }
    toasts_->success(QStringLiteral("Saved %1 active mods").arg(saved->size()));
    co_return true;
}

QCoro::Task<bool> MainWindow::confirmLaunch(bool AppConfig::* setting, const QString& title,
        const QString& text, const QString& info, const QString& accept) {
    const auto [result, dialog] = co_await awaitDialog<LaunchConfirmDialog>(this, title, text, info, accept);
    if (result != QMessageBox::Ok) co_return false;
    if (dialog->dontAskAgain()) {
        auto config = ctx_.config();
        config.*setting = false;
        const auto saved = ctx_.configService().save(QStringLiteral("config.json"), config);
        if (!saved) { toasts_->error(saved.error().message); co_return false; }
        ctx_.updateConfig(std::move(config));
    }
    co_return true;
}

QCoro::Task<void> MainWindow::launchGame() {
    if (header_controller_->launchState() != QStringLiteral("idle")) co_return;
    header_controller_->setLaunchState(QStringLiteral("launching"));
    const QPointer<MainWindow> guard(this);
    auto& launcher = ctx_.gameLauncher();
    const auto config = ctx_.config();
    const bool running = config.launch_confirm_running && co_await QtConcurrent::run([&launcher] { return launcher.is_running(); });
    if (!guard || closing_) co_return;
    const int errors = ctx_.diagnosticsService().active_error_count();
    const auto abort = [this] { header_controller_->setLaunchState(QStringLiteral("idle")); };
    if (running && !co_await confirmLaunch(&AppConfig::launch_confirm_running,
            QStringLiteral("Game Already Running"), QStringLiteral("RimWorld is already running."),
            QStringLiteral("A second copy can overwrite the first one's saves and settings."), QStringLiteral("Launch Anyway"))) { abort(); co_return; }
    if (config.launch_confirm_errors && errors > 0 && !co_await confirmLaunch(&AppConfig::launch_confirm_errors,
            QStringLiteral("Mod List Has Errors"), QStringLiteral("%1 active mod(s) have errors.").arg(errors),
            QStringLiteral("Missing dependencies or wrong load order can crash the game."), QStringLiteral("Launch Anyway"))) { abort(); co_return; }
    if (config.launch_confirm_unsaved && unsaved_ && !co_await confirmLaunch(&AppConfig::launch_confirm_unsaved,
            QStringLiteral("Unsaved Changes"), QStringLiteral("The active mod list has unsaved changes."),
            QStringLiteral("The list is saved to ModsConfig.xml before the game starts."), QStringLiteral("Save and Launch"))) { abort(); co_return; }
    if (!co_await writeActiveLayout()) toasts_->warning(QStringLiteral("Config folder not set — mod list won't be saved"));
    const auto result = co_await launcher.launch(app_.uiPrefs().launch_strategy);
    if (!guard || closing_) co_return;
    if (!result) { toasts_->warning(result.error().message); abort(); co_return; }
    toasts_->success(*result);
    const auto after = ctx_.config().after_launch;
    if (!co_await launcher.wait_for_start()) {
        if (guard && !closing_) { toasts_->warning(QStringLiteral("Game did not start")); abort(); }
        co_return;
    }
    if (!guard || closing_) co_return;
    header_controller_->setLaunchState(QStringLiteral("running"));
    if (after == AfterLaunch::Close && !unsaved_) { close_confirmed_ = true; close(); co_return; }
    if (after == AfterLaunch::Minimize) showMinimized();
    const auto code = co_await launcher.wait_for_exit();
    if (!guard || closing_) co_return;
    if (code && *code) toasts_->warning(QStringLiteral("Game exited with code %1").arg(*code));
    else toasts_->info(QStringLiteral("Game closed"));
    if (after == AfterLaunch::Minimize) { showNormal(); activateWindow(); }
    abort();
}

QCoro::Task<void> MainWindow::selectMod(QString uuid) {
    selected_uuid_ = uuid;
    if (!selection_) co_return;
    if (uuid.isEmpty()) selection_->clear();
    else co_await selection_->show(std::move(uuid));
}

QCoro::Task<void> MainWindow::requestMod(QString uuid) {
    if (!mods_view_) co_return;
    if (mods_view_->mod_list->select_uuid(uuid)) co_return;
    mods_view_->mod_list->search_input->clear();
    mods_view_->sidebar->select_all_entry();
    if (!mods_view_->mod_list->select_uuid(uuid)) co_await selectMod(std::move(uuid));
}

}  // namespace px::modrim
