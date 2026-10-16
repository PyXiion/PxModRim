#pragma once

#ifdef PX_HAS_STEAM

#include <variant>

#include <QJsonValue>
#include <QSet>

#include "core/downloads/steam.hpp"
#include "ui/context.hpp"
#include "ui/plugins/steam_workshop/download_queue_model.hpp"

namespace px::modrim {

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
    QCoro::Task<Result<QJsonValue>> fetch_mod_deps(QString id);

    Event<QStringList> badges_refresh_requested;
    Event<QStringList> active_refresh_requested;
    Event<SidebarSync> sidebar_sync_requested;
    Event<DownloadItemStatus> item_status_changed;
    Event<bool> download_busy_changed;
    Event<QString> uncheck_mod_requested;
    Event<std::monostate> clear_checked_requested;

private:
    void disconnect_events();
    void refresh_cached_ids();
    void notify_sidebar();
    void on_item_status(const DownloadItemStatus& status);
    void on_download_finished(const DownloadResult& result);
    CoreContext* core_ = nullptr;
    AppContext* app_ = nullptr;
    SteamDownloader* service_ = nullptr;
    QSet<QString> installed_ids_;
    QSet<QString> active_ids_;
    CheckedDownloads checked_ids_;
    QHash<QString, QString> statuses_;
    bool own_download_ = false;
    Event<std::monostate>::Id mods_id_ = 0;
    Event<QStringList>::Id active_id_ = 0;
    Event<DownloadItemStatus>::Id status_id_ = 0;
    Event<DownloadResult>::Id finished_id_ = 0;
    Event<bool>::Id busy_id_ = 0;
};

}  // namespace px::modrim
#endif
