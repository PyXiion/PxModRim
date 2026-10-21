#include "ui/window/main_window.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QResizeEvent>
#include <QScopeGuard>
#include <QScreen>
#include <QSplitter>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantMap>
#include <QWindow>
#include <ranges>

#include <QCoro/QCoroFuture>
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
#include "ui/window/geometry.hpp"
#include "ui/window/menu_bar.hpp"

namespace px::modrim {

UnsavedChangesDialog::UnsavedChangesDialog(QWidget* parent)
    : QMessageBox(parent) {
  setIcon(Question);
  setWindowTitle(QStringLiteral("Unsaved Changes"));
  setText(QStringLiteral("The active mod list has unsaved changes."));
  setInformativeText(QStringLiteral(
      "Save before closing, discard the changes, or cancel closing."));
  setStandardButtons(Save | Discard | Cancel);
  setDefaultButton(Save);
}

namespace {
const QList<QPair<QString, QString>> kQmlShortcuts{
    {QStringLiteral("Up / Down"), QStringLiteral("Navigate mod list")},
    {QStringLiteral("Shift+Up / Shift+Down"),
     QStringLiteral("Extend mod selection")},
    {QStringLiteral("Return / Space"), QStringLiteral("Toggle selected mods")},
    {QStringLiteral("Ctrl+A"), QStringLiteral("Select all mods")},
};

class LaunchConfirmDialog : public QMessageBox {
 public:
  LaunchConfirmDialog(QWidget* parent, const QString& title,
                      const QString& text, const QString& info,
                      const QString& accept);
  bool dontAskAgain() const;
};
}  // namespace

LaunchConfirmDialog::LaunchConfirmDialog(QWidget* parent, const QString& title,
                                         const QString& text,
                                         const QString& info,
                                         const QString& accept)
    : QMessageBox(parent) {
  setIcon(Warning);
  setWindowTitle(title);
  setText(text);
  setInformativeText(info);
  setStandardButtons(Ok | Cancel);
  button(Ok)->setText(accept);
  setDefaultButton(Cancel);
  setCheckBox(new QCheckBox(QStringLiteral("Don't ask again"), this));
}

bool LaunchConfirmDialog::dontAskAgain() const {
  return checkBox()->isChecked();
}

MainWindow::MainWindow(AppContext& app, QWidget* parent)
    : QMainWindow(parent),
      m_app(app),
      m_ctx(app.coreContext()),
      m_startedWithoutGamePath(m_ctx.config().paths.game.empty()),
      m_engine(createQmlEngine(this)),
      m_savedActive(m_ctx.loadedActiveUuids()),
      m_updates(appVersion(), m_ctx.network()) {
  setWindowTitle(QStringLiteral("PxModRim[*]"));
  setWindowIcon(QIcon(QStringLiteral(":/qt/qml/PxModRim/assets/logo.svg")));
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                 Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint |
                 Qt::WindowCloseButtonHint);
  setFocusPolicy(Qt::ClickFocus);
  m_engine->rootContext()->setContextProperty(QStringLiteral("Theme"),
                                              new Theme(m_engine));

  const QVariantMap tooltips{
      {"refresh", actionSpec(ActionId::kRefresh).tooltip()},
      {"sort", actionSpec(ActionId::kAutoSort).tooltip()},
      {"save", actionSpec(ActionId::kSave).tooltip()},
      {"update_mods", actionSpec(ActionId::kUpdateMods).tooltip()},
  };
  m_headerController = new HeaderController(
      windowFlags().testFlag(Qt::FramelessWindowHint),
      static_cast<int>(m_app.uiPrefs().launch_strategy), appVersion(), tooltips,
      m_ctx.downloads().available(), this);
  auto* header = new HeaderPanel(*m_headerController, m_engine, this);
  setupActions();
  m_menu = new MenuBar(m_actions, this);
  m_menu->hide();
  connect(m_headerController, &HeaderController::refreshRequested, this,
          [this] { reloadMods(); });
  connect(m_headerController, &HeaderController::sortRequested, this,
          [this] { autoSort(); });
  connect(m_headerController, &HeaderController::saveRequested, this,
          [this] { saveActiveMods(); });
  connect(m_headerController, &HeaderController::launchRequested, this,
          [this] { launchGame(); });
  connect(m_headerController, &HeaderController::updateModsRequested, this,
          [this] { updateMods(); });
  connect(m_headerController, &HeaderController::downloadsRequested, this,
          &MainWindow::openDownloads);
  connect(m_headerController, &HeaderController::minimizeRequested, this,
          &QWidget::showMinimized);
  connect(m_headerController, &HeaderController::maximizeRequested, this,
          [this] { isMaximized() ? showNormal() : showMaximized(); });
  connect(m_headerController, &HeaderController::closeRequested, this,
          &QWidget::close);
  connect(m_headerController, &HeaderController::dragStartRequested, this,
          [this] {
            if (windowHandle()) windowHandle()->startSystemMove();
          });
  connect(m_headerController, &HeaderController::strategyChanged, this,
          [this](int index) {
            const auto strategy = static_cast<LaunchStrategy>(index);
            if (m_app.uiPrefs().launch_strategy != strategy) {
              m_app.uiPrefs().launch_strategy = strategy;
              savePrefs();
            }
          });

  auto* outer = new QWidget(this);
  outer->setObjectName(QStringLiteral("outerContainer"));
  auto* layout = new QVBoxLayout(outer);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(header);
  layout->addWidget(m_menu);
  QVariantList tabs;
  for (const auto& view : m_app.railViews()) {
    tabs.append(QVariantMap{{"viewId", QString(view.meta.viewId)},
                            {"icon", QString(view.meta.iconName)},
                            {"label", QString(view.meta.label)}});
  }
  QVariantList help;
  for (const auto id :
       {ActionId::kReportIssue, ActionId::kUploadLogs, ActionId::kOpenLogs,
        ActionId::kShortcuts, ActionId::kCheckUpdates, ActionId::kAbout}) {
    const auto& spec = actionSpec(id);
    help.append(QVariantMap{{"id", spec.key}, {"label", spec.label}});
  }
  m_splitter = new QSplitter(Qt::Horizontal, outer);
  m_splitter->setContentsMargins(0, 0, 0, 0);
  m_splitter->setHandleWidth(1);
  m_rail = new ViewRailPanel(tabs, help, m_engine, m_splitter);
  m_rail->setObjectName(QStringLiteral("viewRail"));
  m_stack = new QStackedWidget(m_splitter);
  m_stack->setObjectName(QStringLiteral("viewStack"));
  m_splitter->addWidget(m_rail);
  m_splitter->addWidget(m_stack);
  m_splitter->setStretchFactor(0, 0);
  m_splitter->setStretchFactor(1, 1);
  m_splitter->setCollapsible(0, false);
  m_splitter->setCollapsible(1, false);
  const int railWidth =
      m_app.uiPrefs().rail_collapsed ? kRailMinWidth : kRailMaxWidth;
  m_splitter->setSizes({railWidth, width() - railWidth});
  connect(m_rail, &ViewRailPanel::currentChanged, this,
          &MainWindow::selectView);
  connect(m_rail, &ViewRailPanel::hovered, this, &MainWindow::preloadView);
  connect(m_rail, &ViewRailPanel::settingsRequested, this,
          [this] { m_app.navigate(buildRoute(QString(kSettingsViewId))); });
  connect(m_rail, &ViewRailPanel::helpActionRequested, this,
          [this](const QString& key) {
            for (const auto& spec : actionSpecs())
              if (spec.key == key) m_actions.at(spec.id)->trigger();
          });
  connect(m_splitter, &QSplitter::splitterMoved, this, [this] { snapRail(); });
  for (const auto& spec : m_app.railViews()) {
    auto* view = spec.create(m_ctx, m_engine, m_stack, m_app);
    m_views.push_back(view);
    m_stack->addWidget(view);
    if (spec.meta.viewId == QLatin1StringView("mods"))
      m_modsView = qobject_cast<ModsViewPanel*>(view);
  }
  if (m_modsView) {
    m_selection =
        std::make_unique<ModSelectionPresenter>(m_ctx, *m_modsView->modInfo);
    connect(m_modsView, &ModsViewPanel::entrySelected, this,
            [this](const view::SidebarEntry& entry) {
              m_modsView->applySidebarEntry(entry);
            });
    connect(m_modsView, &ModsViewPanel::modSelected, this,
            [this](const QString& uuid) { selectMod(uuid); });
    connect(m_modsView->modInfo, &ModInfoPanel::modRequested, this,
            [this](const QString& uuid) { requestMod(uuid); });
  }
  layout->addWidget(m_splitter, 1);
  setCentralWidget(outer);
  m_toasts = new ToastManager(outer);
  m_toasts->resizeToParent();
  m_app.setToasts(m_toasts);
  m_app.setNavigator([this](const QString& url) { openRoute(url); });
  m_activeConnection = m_ctx.activeStateChanged().connect(
      [this](const auto&) { refreshUnsavedState(); });
  m_modsConnection = m_ctx.modService().modsChanged.connect(
      [this](const auto&) { onModsReloaded(); });
  auto& diagnostics = m_ctx.diagnosticsService();
  m_diagnosticsConnection =
      diagnostics.diagnosticsSummaryChanged.connect([this](const auto&) {
        if (!m_modsView) return;
        m_modsView->applyCurrentSidebarFilter();
        if (!m_selectedUuid.isEmpty())
          m_modsView->modInfo->setIssues(
              m_ctx.diagnosticsService().issuesFor(m_selectedUuid));
      });
  m_statusConnection = diagnostics.statusMessageChanged.connect(
      [this](const QString& message) { m_toasts->info(message); });
  m_taskConnection = diagnostics.backgroundTaskChanged.connect(
      [this](const QString& text) { m_headerController->setTaskText(text); });
  auto& downloads = m_ctx.downloads();
  if (downloads.available()) {
    m_downloadBusyConnection = downloads.busyChanged.connect([this](bool busy) {
      m_headerController->setDownloadsBusy(busy);
      m_actions.at(ActionId::kUpdateMods)->setEnabled(!busy);
      if (busy)
        m_headerController->setDownloadsProgress(
            QStringLiteral("Updating mods…"), 0, 0);
    });
    m_downloadProgressConnection = downloads.downloadProgress.connect(
        [this](const DownloadProgress& progress) {
          m_headerController->setDownloadsProgress(
              QStringLiteral("Updating mods: %1 / %2 · %3 MB")
                  .arg(progress.completed)
                  .arg(progress.total)
                  .arg(progress.bytesDone / 1048576.0, 0, 'f', 0),
              progress.completed, progress.total);
        });
    m_downloadFinishedConnection = downloads.downloadFinished.connect(
        [this](const auto& result) { onDownloadsFinished(result); });
    m_downloadStatusConnection = downloads.statusMessageChanged.connect(
        [this](const QString& message) { m_toasts->info(message); });
  }
  qApp->installEventFilter(this);
  if (!m_views.empty()) showView(0);
  refreshUnsavedState();
  const QRect available = screen()->availableGeometry();
  const QRect centred(available.center() - QPoint(700, 425), QSize(1400, 850));
  setGeometry(fitWindowGeometry(centred, available));
}

MainWindow::~MainWindow() {
  qApp->removeEventFilter(this);
  m_app.setNavigator({});
  m_app.setToasts(nullptr);
  m_ctx.activeStateChanged().disconnect(m_activeConnection);
  m_ctx.modService().modsChanged.disconnect(m_modsConnection);
  auto& diagnostics = m_ctx.diagnosticsService();
  diagnostics.diagnosticsSummaryChanged.disconnect(m_diagnosticsConnection);
  diagnostics.statusMessageChanged.disconnect(m_statusConnection);
  diagnostics.backgroundTaskChanged.disconnect(m_taskConnection);
  auto& downloads = m_ctx.downloads();
  downloads.busyChanged.disconnect(m_downloadBusyConnection);
  downloads.downloadProgress.disconnect(m_downloadProgressConnection);
  downloads.downloadFinished.disconnect(m_downloadFinishedConnection);
  downloads.statusMessageChanged.disconnect(m_downloadStatusConnection);
  const auto dialogs =
      findChildren<QDialog*>(QString(), Qt::FindDirectChildrenOnly);
  for (auto* dialog : dialogs) delete dialog;
  // QuickWidgets must release their scenes before the shared engine is
  // destroyed.
  delete takeCentralWidget();
  delete m_engine;
}

void MainWindow::setupActions() {
  m_actions = createActions(*this);
  const auto bind = [this](ActionId id, auto handler) {
    connect(m_actions.at(id), &QAction::triggered, this, handler);
  };
  bind(ActionId::kSave, [this] { saveActiveMods(); });
  bind(ActionId::kRestore, [this] { restoreSnapshot(); });
  bind(ActionId::kSettings,
       [this] { m_app.navigate(buildRoute(QString(kSettingsViewId))); });
  bind(ActionId::kQuit, [this] { close(); });
  bind(ActionId::kRefresh, [this] { reloadMods(); });
  bind(ActionId::kFullRescan, [this] { reloadMods(true); });
  bind(ActionId::kAutoSort, [this] { autoSort(); });
  bind(ActionId::kUpdateMods, [this] { updateMods(); });
  bind(ActionId::kFocusSearch, [this] { focusSearch(); });
  bind(ActionId::kNextView, [this] { cycleView(1); });
  bind(ActionId::kPrevView, [this] { cycleView(-1); });
  bind(ActionId::kFullscreen,
       [this] { isFullScreen() ? showNormal() : showFullScreen(); });
  bind(ActionId::kReportIssue,
       [] { QDesktopServices::openUrl(QUrl(QString(kIssuesUrl))); });
  bind(ActionId::kOpenLogs, [] {
    QDesktopServices::openUrl(QUrl::fromLocalFile(
        QDir(configDir()).filePath(QStringLiteral("logs"))));
  });
  bind(ActionId::kUploadLogs,
       [this] { handleUploadReport(this, m_ctx, *m_toasts); });
  bind(ActionId::kShortcuts, [this] {
    awaitDialog<KeyboardShortcutsDialog>(shortcutRows() + kQmlShortcuts, this);
  });
  bind(ActionId::kCheckUpdates, [this] { checkUpdates(true); });
  bind(ActionId::kAbout,
       [this] { awaitDialog<AboutPanel>(this, m_ctx, *m_toasts); });
  m_actions.at(ActionId::kUpdateMods)
      ->setEnabled(m_ctx.downloads().available());
  for (int index = 0; index < 9; ++index) {
    auto* action = new QAction(this);
    action->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(index + 1)));
    connect(action, &QAction::triggered, this,
            [this, index] { selectView(index); });
    addAction(action);
  }
}

void MainWindow::setAppQuitCallback(std::function<void()> callback) {
  m_quit = std::move(callback);
}

void MainWindow::requestRestart() {
  if (m_closing || m_closePromptOpen) return;
  m_restartRequested = true;
  close();
}

void MainWindow::openRoute(const QString& url) {
  const auto route = parseRoute(url);
  if (!route) return;
  if (route->viewId == kSettingsViewId) {
    openSettings();
    return;
  }
  const auto& specs = m_app.railViews();
  for (std::size_t i = 0; i < m_views.size(); ++i) {
    if (route->viewId == specs[i].meta.viewId) {
      showView(static_cast<int>(i));
      m_views[i]->openRoute(route->path);
      return;
    }
  }
}

void MainWindow::selectView(int index) {
  if (index >= 0 && index < m_stack->count())
    m_app.navigate(buildRoute(QString(m_app.railViews()[index].meta.viewId)));
}

void MainWindow::showView(int index) {
  m_rail->setCurrent(index);
  m_stack->setCurrentIndex(index);
  m_headerController->setDownloadsProgressShown(
      !m_app.railViews()[index].meta.listsDownloads);
  preloadView(index - 1);
  preloadView(index + 1);
}

void MainWindow::preloadView(int index) {
  if (index >= 0 && index < static_cast<int>(m_views.size()))
    m_views[index]->preload();
}

void MainWindow::cycleView(int direction) {
  const int count = m_stack->count();
  if (count)
    selectView(((m_stack->currentIndex() + direction) % count + count) % count);
}

void MainWindow::openDownloads() {
  for (const auto& view : m_app.railViews()) {
    if (view.meta.listsDownloads) {
      m_app.navigate(buildRoute(QString(view.meta.viewId)));
      return;
    }
  }
}

void MainWindow::focusSearch() {
  auto* search = m_stack->currentWidget()
                     ? m_stack->currentWidget()->findChild<QLineEdit*>(
                           QStringLiteral("searchInput"))
                     : nullptr;
  if (search)
    search->setFocus();
  else if (m_modsView)
    m_modsView->modList->searchInput->setFocus();
}

void MainWindow::savePrefs() {
  const auto result = saveUiPrefs(m_app.uiPrefs(), m_ctx.configService());
  if (!result) m_toasts->error(result.error().message);
}

void MainWindow::snapRail() {
  const int target =
      m_rail->width() >= kRailCollapseWidth ? kRailMaxWidth : kRailMinWidth;
  m_splitter->setSizes({target, m_splitter->width() - target});
  const bool collapsed = target == kRailMinWidth;
  if (m_app.uiPrefs().rail_collapsed != collapsed) {
    m_app.uiPrefs().rail_collapsed = collapsed;
    savePrefs();
  }
}

void MainWindow::refreshUnsavedState() {
  m_unsaved = m_ctx.activeUuids() != m_savedActive;
  m_headerController->setUnsavedChanges(m_unsaved);
  setWindowModified(m_unsaved);
}

void MainWindow::onModsReloaded() {
  m_savedActive = m_ctx.loadedActiveUuids();
  refreshUnsavedState();
}

bool MainWindow::eventFilter(QObject* object, QEvent* event) {
  if (event->type() == QEvent::KeyPress) {
    auto* widget = qobject_cast<QWidget*>(object);
    if (widget && widget->window() == this) {
      const int key = static_cast<QKeyEvent*>(event)->key();
      if (key == Qt::Key_Alt) {
        m_menu->setVisible(!m_menu->isVisible());
        if (m_menu->isVisible()) m_menu->setFocus();
        return true;
      }
      if (key == Qt::Key_Escape && m_menu->isVisible()) {
        m_menu->hide();
        return true;
      }
    }
  }
  return QMainWindow::eventFilter(object, event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if (m_unsaved && !m_closeConfirmed) {
    event->ignore();
    if (!m_closePromptOpen) {
      m_closePromptOpen = true;
      confirmClose();
    }
    return;
  }
  m_closing = true;
  for (auto* view : m_views) view->teardown();
  event->accept();
  if (m_quit) m_quit();
}

QCoro::Task<void> MainWindow::confirmClose() {
  const auto [result, dialog] =
      co_await awaitDialog<UnsavedChangesDialog>(this);
  m_closePromptOpen = false;
  bool proceed = result == QMessageBox::Discard;
  if (result == QMessageBox::Save) proceed = co_await saveActiveMods();
  if (!proceed) {
    m_restartRequested = false;
    co_return;
  }
  m_closeConfirmed = true;
  close();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  if (m_toasts) m_toasts->resizeToParent();
}

void MainWindow::changeEvent(QEvent* event) {
  QMainWindow::changeEvent(event);
  if (event->type() == QEvent::WindowStateChange && m_headerController)
    m_headerController->setMaximized(isMaximized());
}

QCoro::Task<void> MainWindow::reloadMods(bool full) {
  m_toasts->info(full ? QStringLiteral("Rescanning all mods…")
                      : QStringLiteral("Refreshing mods…"));
  const auto count = co_await m_app.refreshMods(full);
  if (!count) {
    m_toasts->error(count.error().message);
    co_return;
  }
  if (!*count) {
    m_toasts->warning(QStringLiteral("No mods found"));
    co_return;
  }
  m_toasts->success((full ? QStringLiteral("Rescanned %1 mods")
                          : QStringLiteral("Reloaded %1 mods"))
                        .arg(*count));
}

QCoro::Task<void> MainWindow::autoSort() {
  const auto [count, elapsed] = co_await m_ctx.autoSort();
  m_toasts->success(QStringLiteral("Sorted %1 mods in %2ms")
                        .arg(count)
                        .arg(elapsed, 0, 'f', 0));
}

QCoro::Task<void> MainWindow::updateMods() {
  auto& downloads = m_ctx.downloads();
  if (!downloads.available()) co_return;
  if (downloads.isDownloading()) {
    downloads.cancel();
    co_return;
  }
  const auto ids = downloads.updatableIds(m_ctx.allMods().values());
  if (ids.isEmpty()) {
    m_toasts->info(QStringLiteral("No downloaded mods to update"));
    co_return;
  }
  if (!co_await confirmUpdate(ids.size(), this)) co_return;
  m_toasts->info(QStringLiteral("Updating %1 mods…").arg(ids.size()));
  const auto result = co_await downloads.downloadMods(ids);
  if (!result) m_toasts->error(result.error().message);
}

void MainWindow::onDownloadsFinished(const DownloadResult& result) {
  const auto ok = result.succeeded.size(), failed = result.failed.size(),
             changed = result.changed.size();
  if (!ok && !failed) {
    m_toasts->info(QStringLiteral("Download cancelled"));
    return;
  }
  const QString summary =
      QStringLiteral("%1 updated, %2 unchanged").arg(changed).arg(ok - changed);
  if (failed) {
    const auto names = m_ctx.downloads().namesById(m_ctx.allMods().values());
    QStringList shown;
    for (const auto& id : result.failed | std::views::take(3))
      shown.append(names.value(id, id));
    m_toasts->warning(QStringLiteral("%1, %2 failed: %3%4")
                          .arg(summary)
                          .arg(failed)
                          .arg(shown.join(QStringLiteral(", ")),
                               failed > 3 ? QStringLiteral("…") : QString()));
  } else
    m_toasts->success(summary);
  if (changed) m_app.refreshMods();
}

QCoro::Task<void> MainWindow::openSettings() {
  if (m_settingsOpen) co_return;
  m_settingsOpen = true;
  const auto finish = qScopeGuard([this] { m_settingsOpen = false; });
  const auto [result, dialog] =
      co_await awaitDialog<SettingsPanel>(m_app, m_engine, this);
  if (result != QDialog::Accepted) co_return;
  auto config = dialog->getConfig();
  const auto saved =
      m_ctx.configService().save(QStringLiteral("config.json"), config);
  if (!saved) {
    m_toasts->error(saved.error().message);
    co_return;
  }
  // Other prefs may have changed while Settings was open (e.g. skipping an
  // update), so only the scale comes from the dialog.
  auto prefs = m_app.uiPrefs();
  prefs.ui_scale = dialog->getUiScale();
  const auto prefsSaved = saveUiPrefs(prefs, m_ctx.configService());
  if (!prefsSaved) {
    m_toasts->error(prefsSaved.error().message);
    co_return;
  }
  m_app.uiPrefs() = prefs;
  m_ctx.updateConfig(config);
  m_ctx.resetProviders(config.paths);
  const auto& initialScale = m_app.initialScale();
  if (initialScale.source == ScaleSource::kSaved &&
      prefs.ui_scale != initialScale.scale) {
    m_toasts->info(
        QStringLiteral("Restart PxModRim to apply the interface scale."), 0,
        QStringLiteral("Restart now"), [this] { requestRestart(); });
  }
  const auto reloaded = co_await m_ctx.modService().reload();
  if (!reloaded) {
    m_toasts->error(reloaded.error().message);
    co_return;
  }
  if (m_startedWithoutGamePath && !m_app.pluginsInitialized() &&
      !m_ctx.config().paths.game.empty()) {
    if (auto initialized = co_await m_app.initAll(); !initialized) {
      m_toasts->error(initialized.error().message);
      co_return;
    }
    startStartupUpdateCheck();
  }
  m_toasts->success(QStringLiteral("Settings saved"));
}

QCoro::Task<void> MainWindow::restoreSnapshot() {
  const auto snapshots = m_ctx.modService().getSnapshots();
  if (snapshots.isEmpty()) {
    m_toasts->warning(QStringLiteral("No saved mod lists are available"));
    co_return;
  }
  const auto [result, dialog] =
      co_await awaitDialog<RestoreSnapshotDialog>(snapshots, this);
  if (result != QDialog::Accepted) co_return;
  const auto chosen = dialog->selectedSnapshot();
  if (!chosen) co_return;
  const auto [confirmed, confirmation] =
      co_await awaitDialog<ConfirmRestoreDialog>(QFileInfo(*chosen).fileName(),
                                                 this);
  if (confirmed != QMessageBox::Yes) co_return;
  if (co_await m_ctx.modService().restoreSnapshot(*chosen))
    m_toasts->success(QStringLiteral("Restored mod list from %1")
                          .arg(QFileInfo(*chosen).fileName()));
  else
    m_toasts->error(
        QStringLiteral("The saved mod list is invalid or cannot be read"));
}

void MainWindow::startStartupUpdateCheck() { checkUpdates(false); }

QCoro::Task<void> MainWindow::checkUpdates(bool manual) {
  if (m_checkingUpdates) co_return;
  m_checkingUpdates = true;
  const QPointer<MainWindow> guard(this);
  const auto release = co_await m_updates.check();
  if (!guard || m_closing) co_return;
  if (!release) {
    m_checkingUpdates = false;
    if (manual) m_toasts->error(QStringLiteral("Could not check for updates"));
    co_return;
  }
  if (!release->has_value()) {
    m_checkingUpdates = false;
    if (manual) m_toasts->success(QStringLiteral("PxModRim is up to date"));
    co_return;
  }
  const auto info = **release;
  if (!manual && info.tag.toStdString() == m_app.uiPrefs().skipped_update_tag) {
    m_checkingUpdates = false;
    co_return;
  }
  const auto [result, dialog] = co_await awaitDialog<UpdateDialog>(
      info, m_updates.currentVersion(), this);
  if (!guard || m_closing) co_return;
  m_checkingUpdates = false;
  if (result == QDialog::Accepted)
    QDesktopServices::openUrl(QUrl(info.url));
  else if (result == kSkipResult) {
    m_app.uiPrefs().skipped_update_tag = info.tag.toStdString();
    savePrefs();
  }
}

QCoro::Task<std::optional<QStringList>> MainWindow::writeActiveLayout() {
  const auto active = m_ctx.activeUuids();
  if (!co_await m_ctx.modService().saveActiveLayout(active))
    co_return std::nullopt;
  m_savedActive = active;
  refreshUnsavedState();
  co_return active;
}

QCoro::Task<bool> MainWindow::saveActiveMods() {
  const auto saved = co_await writeActiveLayout();
  if (!saved) {
    m_toasts->warning(QStringLiteral("Config folder not set"));
    co_return false;
  }
  m_toasts->success(QStringLiteral("Saved %1 active mods").arg(saved->size()));
  co_return true;
}

QCoro::Task<bool> MainWindow::confirmLaunch(bool AppConfig::* setting,
                                            const QString& title,
                                            const QString& text,
                                            const QString& info,
                                            const QString& accept) {
  const auto [result, dialog] = co_await awaitDialog<LaunchConfirmDialog>(
      this, title, text, info, accept);
  if (result != QMessageBox::Ok) co_return false;
  if (dialog->dontAskAgain()) {
    auto config = m_ctx.config();
    config.*setting = false;
    const auto saved =
        m_ctx.configService().save(QStringLiteral("config.json"), config);
    if (!saved) {
      m_toasts->error(saved.error().message);
      co_return false;
    }
    m_ctx.updateConfig(std::move(config));
  }
  co_return true;
}

QCoro::Task<void> MainWindow::launchGame() {
  if (m_headerController->launchState() != QStringLiteral("idle")) co_return;
  m_headerController->setLaunchState(QStringLiteral("launching"));
  const QPointer<MainWindow> guard(this);
  auto& launcher = m_ctx.gameLauncher();
  const auto config = m_ctx.config();
  const bool running =
      config.launch_confirm_running &&
      co_await QtConcurrent::run([&launcher] { return launcher.isRunning(); });
  if (!guard || m_closing) co_return;
  const int errors = m_ctx.diagnosticsService().activeErrorCount();
  const auto abort = [this] {
    m_headerController->setLaunchState(QStringLiteral("idle"));
  };
  if (running &&
      !co_await confirmLaunch(&AppConfig::launch_confirm_running,
                              QStringLiteral("Game Already Running"),
                              QStringLiteral("RimWorld is already running."),
                              QStringLiteral("A second copy can overwrite the "
                                             "first one's saves and settings."),
                              QStringLiteral("Launch Anyway"))) {
    abort();
    co_return;
  }
  if (config.launch_confirm_errors && errors > 0 &&
      !co_await confirmLaunch(
          &AppConfig::launch_confirm_errors,
          QStringLiteral("Mod List Has Errors"),
          QStringLiteral("%1 active mod(s) have errors.").arg(errors),
          QStringLiteral(
              "Missing dependencies or wrong load order can crash the game."),
          QStringLiteral("Launch Anyway"))) {
    abort();
    co_return;
  }
  if (config.launch_confirm_unsaved && m_unsaved &&
      !co_await confirmLaunch(
          &AppConfig::launch_confirm_unsaved, QStringLiteral("Unsaved Changes"),
          QStringLiteral("The active mod list has unsaved changes."),
          QStringLiteral(
              "The list is saved to ModsConfig.xml before the game starts."),
          QStringLiteral("Save and Launch"))) {
    abort();
    co_return;
  }
  if (!co_await writeActiveLayout())
    m_toasts->warning(
        QStringLiteral("Config folder not set — mod list won't be saved"));
  const auto result = co_await launcher.launch(m_app.uiPrefs().launch_strategy);
  if (!guard || m_closing) co_return;
  if (!result) {
    m_toasts->warning(result.error().message);
    abort();
    co_return;
  }
  m_toasts->success(*result);
  const auto after = m_ctx.config().after_launch;
  if (!co_await launcher.waitForStart()) {
    if (guard && !m_closing) {
      m_toasts->warning(QStringLiteral("Game did not start"));
      abort();
    }
    co_return;
  }
  if (!guard || m_closing) co_return;
  m_headerController->setLaunchState(QStringLiteral("running"));
  if (after == AfterLaunch::kClose && !m_unsaved) {
    m_closeConfirmed = true;
    close();
    co_return;
  }
  if (after == AfterLaunch::kMinimize) showMinimized();
  const auto code = co_await launcher.waitForExit();
  if (!guard || m_closing) co_return;
  if (code && *code)
    m_toasts->warning(QStringLiteral("Game exited with code %1").arg(*code));
  else
    m_toasts->info(QStringLiteral("Game closed"));
  if (after == AfterLaunch::kMinimize) {
    showNormal();
    activateWindow();
  }
  abort();
}

QCoro::Task<void> MainWindow::selectMod(QString uuid) {
  m_selectedUuid = uuid;
  if (!m_selection) co_return;
  if (uuid.isEmpty())
    m_selection->clear();
  else
    co_await m_selection->show(std::move(uuid));
}

QCoro::Task<void> MainWindow::requestMod(QString uuid) {
  if (!m_modsView) co_return;
  if (m_modsView->modList->selectUuid(uuid)) co_return;
  m_modsView->modList->searchInput->clear();
  m_modsView->sidebar->selectAllEntry();
  if (!m_modsView->modList->selectUuid(uuid))
    co_await selectMod(std::move(uuid));
}

}  // namespace px::modrim
