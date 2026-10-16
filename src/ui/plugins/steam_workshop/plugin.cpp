#include "ui/plugins/steam_workshop/plugin.hpp"

#ifdef PX_HAS_STEAM

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <algorithm>
#include <chrono>

#include <QCoro/QCoroNetworkReply>

#include "core/mod_service.hpp"
#include "ui/navigation.hpp"
#include "ui/plugins/steam_workshop/view.hpp"

namespace px::modrim {

SteamWorkshopUiPlugin::SteamWorkshopUiPlugin()
    : UiPlugin(QStringLiteral("steamworkshop"),
               {QStringLiteral("steam_downloader")}) {}
SteamWorkshopUiPlugin::~SteamWorkshopUiPlugin() { disconnectEvents(); }

void SteamWorkshopUiPlugin::setup(AppContext& ctx) {
  m_core = &ctx.coreContext();
  m_app = &ctx;
  m_service = static_cast<SteamDownloader*>(
      m_core->plugins().get(QStringLiteral("steam_downloader")));
  ctx.addRailView<SteamWorkshopViewPanel>();
  m_modsId =
      m_core->modService().modsChanged.connect([this](const std::monostate&) {
        refreshCachedIds();
        badgesRefreshRequested.notify(m_installedIds.values());
        activeRefreshRequested.notify(m_activeIds.values());
      });
  m_activeId = m_core->activeStateChanged().connect([this](const QStringList&) {
    refreshCachedIds();
    badgesRefreshRequested.notify(m_installedIds.values());
    activeRefreshRequested.notify(m_activeIds.values());
  });
  m_statusId = m_service->downloadItemStatusChanged.connect(
      [this](const DownloadItemStatus& status) { onItemStatus(status); });
  m_finishedId = m_service->downloadFinished.connect(
      [this](const DownloadResult& result) { onDownloadFinished(result); });
  m_busyId = m_service->busyChanged.connect(
      [this](bool busy) { downloadBusyChanged.notify(busy); });
}

QCoro::Task<void> SteamWorkshopUiPlugin::init(AppContext&) {
  refreshCachedIds();
  co_return;
}

QCoro::Task<void> SteamWorkshopUiPlugin::shutdown() {
  disconnectEvents();
  co_return;
}

void SteamWorkshopUiPlugin::disconnectEvents() {
  if (!m_core) return;
  m_core->modService().modsChanged.disconnect(m_modsId);
  m_core->activeStateChanged().disconnect(m_activeId);
  m_service->downloadItemStatusChanged.disconnect(m_statusId);
  m_service->downloadFinished.disconnect(m_finishedId);
  m_service->busyChanged.disconnect(m_busyId);
  m_core = nullptr;
  m_app = nullptr;
}

void SteamWorkshopUiPlugin::toggleDownloadChecked(const QString& id,
                                                  const QString& title,
                                                  bool checked) {
  const auto it =
      std::ranges::find(m_checkedIds, id, &CheckedDownloads::value_type::first);
  if (checked) {
    if (it == m_checkedIds.end())
      m_checkedIds.append({id, title});
    else
      it->second = title;
  } else if (it != m_checkedIds.end()) {
    m_checkedIds.erase(it);
  }
  notifySidebar();
}

void SteamWorkshopUiPlugin::batchToggleDownloadChecked(
    const QStringList& ids, const QStringList& titles, bool checked) {
  for (qsizetype pos = 0; pos < ids.size(); ++pos) {
    const auto it = std::ranges::find(m_checkedIds, ids[pos],
                                      &CheckedDownloads::value_type::first);
    if (checked) {
      if (it == m_checkedIds.end())
        m_checkedIds.append({ids[pos], titles[pos]});
      else
        it->second = titles[pos];
    } else if (it != m_checkedIds.end()) {
      m_checkedIds.erase(it);
    }
  }
  if (!ids.isEmpty()) notifySidebar();
}

void SteamWorkshopUiPlugin::toggleActive(const QString& id, bool active) {
  if (!m_core) return;
  for (auto it = m_core->allMods().cbegin(); it != m_core->allMods().cend();
       ++it) {
    if (it.value()->publishedFileId() != id) continue;
    auto current = m_core->activeUuids();
    if (active) {
      if (!current.contains(it.key())) current.append(it.key());
    } else {
      current.removeAll(it.key());
    }
    m_core->setActive(std::move(current));
    return;
  }
}

void SteamWorkshopUiPlugin::batchToggleActive(const QStringList& ids,
                                              bool active) {
  if (!m_core) return;
  QHash<QString, QString> byPublishedId;
  for (auto it = m_core->allMods().cbegin(); it != m_core->allMods().cend();
       ++it) {
    if (auto id = it.value()->publishedFileId())
      byPublishedId.insert(*id, it.key());
  }
  auto current = m_core->activeUuids();
  for (const auto& id : ids) {
    const auto it = byPublishedId.constFind(id);
    if (it == byPublishedId.cend()) continue;
    if (active) {
      if (!current.contains(*it)) current.append(*it);
    } else {
      current.removeAll(*it);
    }
  }
  m_core->setActive(std::move(current));
}

QCoro::Task<void> SteamWorkshopUiPlugin::requestDownload() {
  if (m_checkedIds.isEmpty()) co_return;
  if (!m_core || m_core->config().paths.local.empty()) {
    m_service->statusMessageChanged.notify(
        QStringLiteral("Local mods path is not configured"));
    co_return;
  }
  if (m_service->isDownloading()) {
    m_service->statusMessageChanged.notify(
        QStringLiteral("A download is already running"));
    co_return;
  }
  QStringList ids;
  ids.reserve(m_checkedIds.size());
  for (const auto& [id, title] : m_checkedIds) {
    ids.append(id);
    m_statuses.insert(id, QStringLiteral("queued"));
  }
  m_ownDownload = true;
  if (m_app) m_app->navigate(buildRoute(QStringLiteral("downloads")));
  notifySidebar();
  auto result = co_await m_service->downloadMods(std::move(ids));
  m_ownDownload = false;
  if (!result) m_service->statusMessageChanged.notify(result.error().message);
}

void SteamWorkshopUiPlugin::stopDownload() { m_service->cancel(); }
bool SteamWorkshopUiPlugin::isDownloading() const {
  return m_service->isDownloading();
}

void SteamWorkshopUiPlugin::removeItem(const QString& id) {
  m_checkedIds.removeIf([&id](const auto& item) { return item.first == id; });
  m_statuses.remove(id);
  notifySidebar();
  uncheckModRequested.notify(id);
}

void SteamWorkshopUiPlugin::clearQueue() {
  m_checkedIds.clear();
  m_statuses.clear();
  notifySidebar();
  clearCheckedRequested.notify(std::monostate{});
}

void SteamWorkshopUiPlugin::notifySidebar() {
  sidebarSyncRequested.notify(SidebarSync{m_checkedIds, m_statuses});
}

void SteamWorkshopUiPlugin::syncAll() {
  refreshCachedIds();
  badgesRefreshRequested.notify(m_installedIds.values());
  activeRefreshRequested.notify(m_activeIds.values());
  notifySidebar();
}

void SteamWorkshopUiPlugin::onItemStatus(const DownloadItemStatus& status) {
  if (!m_ownDownload) return;
  m_statuses.insert(status.modId, status.status);
  itemStatusChanged.notify(status);
}

void SteamWorkshopUiPlugin::onDownloadFinished(const DownloadResult& result) {
  if (!m_ownDownload) return;
  for (const auto& id : result.succeeded) {
    m_checkedIds.removeIf([&id](const auto& item) { return item.first == id; });
    m_statuses.remove(id);
  }
  for (auto it = m_statuses.begin(); it != m_statuses.end();) {
    if (it.value() == QLatin1String("queued") ||
        it.value() == QLatin1String("downloading"))
      it = m_statuses.erase(it);
    else
      ++it;
  }
  notifySidebar();
}

void SteamWorkshopUiPlugin::refreshCachedIds() {
  if (!m_core) return;
  m_installedIds.clear();
  m_activeIds.clear();
  const QSet<QString> active(m_core->activeUuids().cbegin(),
                             m_core->activeUuids().cend());
  for (const auto& mod : m_core->allMods()) {
    if (auto id = mod->publishedFileId()) {
      m_installedIds.insert(*id);
      if (active.contains(mod->uuid())) m_activeIds.insert(*id);
    }
  }
}

QCoro::Task<Result<QJsonValue>> SteamWorkshopUiPlugin::fetchModDeps(
    QString id) {
  QUrl url(QStringLiteral("https://deps.modrim.pyxiion.ru/deps"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("id"), id);
  url.setQuery(query);
  QNetworkRequest request(url);
  request.setRawHeader("User-Agent", "PxModRim/1.0");
  request.setTransferTimeout(30000);
  auto* reply = m_core->network().get(request);
  if (!co_await qCoro(reply).waitForFinished(std::chrono::seconds{30})) {
    reply->abort();
    reply->deleteLater();
    co_return std::unexpected(Error{QStringLiteral("Timeout")});
  }
  const auto error = reply->error();
  const auto status =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const auto bytes = reply->readAll();
  const auto message = reply->errorString();
  reply->deleteLater();
  if (error == QNetworkReply::TimeoutError)
    co_return std::unexpected(Error{QStringLiteral("Timeout")});
  if (status && status != 200) co_return QJsonValue(QJsonValue::Null);
  if (error != QNetworkReply::NoError)
    co_return std::unexpected(Error{message});
  if (bytes.isEmpty()) co_return QJsonValue(QJsonValue::Null);
  QJsonParseError parseError;
  const auto document =
      QJsonDocument::fromJson(QByteArray("[") + bytes + ']', &parseError);
  if (parseError.error != QJsonParseError::NoError)
    co_return std::unexpected(Error{parseError.errorString()});
  co_return document.array().at(0);
}

}  // namespace px::modrim
#endif
