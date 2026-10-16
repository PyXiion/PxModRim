#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <QCoro/QCoroTask>
#include <QMainWindow>
#include <QMessageBox>
#include <QStringList>

#include "core/downloads/types.h"
#include "core/services/update_service.h"
#include "ui/context.h"
#include "ui/window/actions.h"

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
    LaunchConfirmDialog(QWidget* parent, const QString& title, const QString& text,
                        const QString& info, const QString& accept);
    bool dontAskAgain() const;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppContext& app, QWidget* parent = nullptr);
    ~MainWindow() override;

    QQmlEngine* qmlEngine() const noexcept { return engine_; }
    void setAppQuitCallback(std::function<void()> callback);
    void startStartupUpdateCheck();
    void openRoute(const QString& url);
    void selectView(int index);
    void cycleView(int direction);
    void refreshUnsavedState();
    bool hasUnsavedChanges() const noexcept { return unsaved_; }
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
    QCoro::Task<bool> confirmLaunch(bool AppConfig::* setting, const QString& title,
                                   const QString& text, const QString& info, const QString& accept);
    QCoro::Task<void> launchGame();
    QCoro::Task<void> selectMod(QString uuid);
    QCoro::Task<void> requestMod(QString uuid);

    AppContext& app_;
    CoreContext& ctx_;
    QQmlEngine* engine_;
    HeaderController* header_controller_ = nullptr;
    HeaderPanel* header_ = nullptr;
    MenuBar* menu_ = nullptr;
    ViewRailPanel* rail_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QSplitter* splitter_ = nullptr;
    ToastManager* toasts_ = nullptr;
    ModsViewPanel* mods_view_ = nullptr;
    std::vector<RailView*> views_;
    std::unique_ptr<ModSelectionPresenter> selection_;
    QString selected_uuid_;
    QStringList saved_active_;
    ActionMap actions_;
    std::function<void()> quit_;
    services::UpdateService updates_;
    bool unsaved_ = false;
    bool close_prompt_open_ = false;
    bool close_confirmed_ = false;
    bool closing_ = false;
    bool settings_open_ = false;
    bool checking_updates_ = false;
    std::uint64_t active_connection_ = 0;
    std::uint64_t mods_connection_ = 0;
    std::uint64_t diagnostics_connection_ = 0;
    std::uint64_t status_connection_ = 0;
    std::uint64_t task_connection_ = 0;
    std::uint64_t download_busy_connection_ = 0;
    std::uint64_t download_progress_connection_ = 0;
    std::uint64_t download_finished_connection_ = 0;
    std::uint64_t download_status_connection_ = 0;
};

}  // namespace px::modrim
