#pragma once

#ifdef PX_HAS_STEAM

#include <QJsonValue>
#include <QSet>
#include <variant>

#include "core/downloads/steam.hpp"
#include "ui/context.hpp"
#include "ui/plugins/steam_workshop/download_queue_model.hpp"

namespace px::modrim {

struct SidebarSync {
  CheckedDownloads checkedIds;
  QHash<QString, QString> statuses;
};

class SteamWorkshopUiPlugin final : public UiPlugin {
 public:
  SteamWorkshopUiPlugin();
  ~SteamWorkshopUiPlugin() override;
  void setup(AppContext& ctx) override;
  QCoro::Task<void> init(AppContext& ctx) override;
  QCoro::Task<void> shutdown() override;

  void toggleDownloadChecked(const QString& id, const QString& title,
                             bool checked);
  void batchToggleDownloadChecked(const QStringList& ids,
                                  const QStringList& titles, bool checked);
  void toggleActive(const QString& id, bool active);
  void batchToggleActive(const QStringList& ids, bool active);
  QCoro::Task<void> requestDownload();
  void stopDownload();
  void removeItem(const QString& id);
  void clearQueue();
  void syncAll();
  bool isDownloading() const;
  QCoro::Task<Result<QJsonValue>> fetchModDeps(QString id);

  Event<QStringList> badgesRefreshRequested;
  Event<QStringList> activeRefreshRequested;
  Event<SidebarSync> sidebarSyncRequested;
  Event<DownloadItemStatus> itemStatusChanged;
  Event<bool> downloadBusyChanged;
  Event<QString> uncheckModRequested;
  Event<std::monostate> clearCheckedRequested;

 private:
  void disconnectEvents();
  void refreshCachedIds();
  void refreshBadges();
  void dropDownload(const QString& id);
  void notifySidebar();
  void onItemStatus(const DownloadItemStatus& status);
  void onDownloadFinished(const DownloadResult& result);
  CoreContext* m_core = nullptr;
  AppContext* m_app = nullptr;
  SteamDownloader* m_service = nullptr;
  QSet<QString> m_installedIds;
  QSet<QString> m_activeIds;
  CheckedDownloads m_checkedIds;
  QHash<QString, QString> m_statuses;
  bool m_ownDownload = false;
  Event<std::monostate>::Id m_modsId = 0;
  Event<QStringList>::Id m_activeId = 0;
  Event<DownloadItemStatus>::Id m_statusId = 0;
  Event<DownloadResult>::Id m_finishedId = 0;
  Event<bool>::Id m_busyId = 0;
};

}  // namespace px::modrim
#endif
