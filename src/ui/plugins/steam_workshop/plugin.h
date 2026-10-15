#pragma once

#ifdef PX_HAS_STEAM

#include <variant>

#include <QJsonValue>
#include <QSet>

#include "core/downloads/steam.h"
#include "ui/context.h"
#include "ui/plugins/steam_workshop/download_queue_model.h"

namespace px::ui {

struct SidebarSync {
    CheckedDownloads checked_ids;
    QHash<QString, QString> statuses;
};

class SteamWorkshopUiPlugin final : public UiPlugin {
public:
    SteamWorkshopUiPlugin();
    ~SteamWorkshopUiPlugin() override;
    void setup(AppContext& ctx) override;
    QCoro::Task<void> init(AppContext& ctx) override;
    QCoro::Task<void> shutdown() override;

    void toggle_download_checked(const QString& id, const QString& title, bool checked);
    void batch_toggle_download_checked(const QStringList& ids, const QStringList& titles, bool checked);
    void toggle_active(const QString& id, bool active);
    void batch_toggle_active(const QStringList& ids, bool active);
    QCoro::Task<void> request_download();
    void stop_download();
    void remove_item(const QString& id);
    void clear_queue();
    void sync_all();
    bool is_downloading() const;
    QCoro::Task<core::Result<QJsonValue>> fetch_mod_deps(QString id);

    core::Event<QStringList> badges_refresh_requested;
    core::Event<QStringList> active_refresh_requested;
    core::Event<SidebarSync> sidebar_sync_requested;
    core::Event<core::DownloadItemStatus> item_status_changed;
    core::Event<bool> download_busy_changed;
    core::Event<QString> uncheck_mod_requested;
    core::Event<std::monostate> clear_checked_requested;

private:
    void disconnect_events();
    void refresh_cached_ids();
    void notify_sidebar();
    void on_item_status(const core::DownloadItemStatus& status);
    void on_download_finished(const core::DownloadResult& result);
    core::CoreContext* core_ = nullptr;
    AppContext* app_ = nullptr;
    core::SteamDownloader* service_ = nullptr;
    QSet<QString> installed_ids_;
    QSet<QString> active_ids_;
    CheckedDownloads checked_ids_;
    QHash<QString, QString> statuses_;
    bool own_download_ = false;
    core::Event<std::monostate>::Id mods_id_ = 0;
    core::Event<QStringList>::Id active_id_ = 0;
    core::Event<core::DownloadItemStatus>::Id status_id_ = 0;
    core::Event<core::DownloadResult>::Id finished_id_ = 0;
    core::Event<bool>::Id busy_id_ = 0;
};

}  // namespace px::ui
#endif
