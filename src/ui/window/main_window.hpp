#pragma once

#include <QMainWindow>
#include <QMessageBox>
#include <QStringList>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <QCoro/QCoroTask>

#include "core/downloads/types.hpp"
#include "core/services/update_service.hpp"
#include "ui/context.hpp"
#include "ui/window/actions.hpp"

class QQmlEngine;
class QSplitter;
class QStackedWidget;

namespace px::modrim {

class HeaderController;
class HeaderPanel;
class MenuBar;
class ModSelectionPresenter;
class ModsViewPanel;
class ToastManager;
class ViewRailPanel;

class UnsavedChangesDialog : public QMessageBox {
 public:
  explicit UnsavedChangesDialog(QWidget* parent);
};

class LaunchConfirmDialog : public QMessageBox {
 public:
  LaunchConfirmDialog(QWidget* parent, const QString& title,
                      const QString& text, const QString& info,
                      const QString& accept);
  bool dontAskAgain() const;
};

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(AppContext& app, QWidget* parent = nullptr);
  ~MainWindow() override;

  QQmlEngine* qmlEngine() const noexcept { return m_engine; }
  void setAppQuitCallback(std::function<void()> callback);
  void startStartupUpdateCheck();
  void openRoute(const QString& url);
  void selectView(int index);
  void cycleView(int direction);
  void refreshUnsavedState();
  bool hasUnsavedChanges() const noexcept { return m_unsaved; }
  QCoro::Task<bool> saveActiveMods();

 protected:
  bool eventFilter(QObject* object, QEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;

 private:
  void setupActions();
  void showView(int index);
  void preloadView(int index);
  void openDownloads();
  void focusSearch();
  void snapRail();
  void savePrefs();
  void onModsReloaded();
  void onDownloadsFinished(const DownloadResult& result);
  QCoro::Task<void> confirmClose();
  QCoro::Task<void> reloadMods(bool full = false);
  QCoro::Task<void> autoSort();
  QCoro::Task<void> updateMods();
  QCoro::Task<void> openSettings();
  QCoro::Task<void> showAbout();
  QCoro::Task<void> showShortcuts();
  QCoro::Task<void> uploadReport();
  QCoro::Task<void> restoreSnapshot();
  QCoro::Task<void> checkUpdates(bool manual);
  QCoro::Task<std::optional<QStringList>> writeActiveLayout();
  QCoro::Task<bool> confirmLaunch(bool AppConfig::* setting,
                                  const QString& title, const QString& text,
                                  const QString& info, const QString& accept);
  QCoro::Task<void> launchGame();
  QCoro::Task<void> selectMod(QString uuid);
  QCoro::Task<void> requestMod(QString uuid);

  AppContext& m_app;
  CoreContext& m_ctx;
  const bool m_startedWithoutGamePath;
  QQmlEngine* m_engine;
  HeaderController* m_headerController = nullptr;
  HeaderPanel* m_header = nullptr;
  MenuBar* m_menu = nullptr;
  ViewRailPanel* m_rail = nullptr;
  QStackedWidget* m_stack = nullptr;
  QSplitter* m_splitter = nullptr;
  ToastManager* m_toasts = nullptr;
  ModsViewPanel* m_modsView = nullptr;
  std::vector<RailView*> m_views;
  std::unique_ptr<ModSelectionPresenter> m_selection;
  QString m_selectedUuid;
  QStringList m_savedActive;
  ActionMap m_actions;
  std::function<void()> m_quit;
  services::UpdateService m_updates;
  bool m_unsaved = false;
  bool m_closePromptOpen = false;
  bool m_closeConfirmed = false;
  bool m_closing = false;
  bool m_settingsOpen = false;
  bool m_checkingUpdates = false;
  std::uint64_t m_activeConnection = 0;
  std::uint64_t m_modsConnection = 0;
  std::uint64_t m_diagnosticsConnection = 0;
  std::uint64_t m_statusConnection = 0;
  std::uint64_t m_taskConnection = 0;
  std::uint64_t m_downloadBusyConnection = 0;
  std::uint64_t m_downloadProgressConnection = 0;
  std::uint64_t m_downloadFinishedConnection = 0;
  std::uint64_t m_downloadStatusConnection = 0;
};

}  // namespace px::modrim
