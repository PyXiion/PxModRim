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

#include "core/downloads/manager.hpp"
#include "core/mod_service.hpp"
#include "ui/components/mod_activation.hpp"
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
  m_modsId = m_core->modService().modsChanged.connect(
      [this](const std::monostate&) { refreshBadges(); });
  m_activeId = m_core->activeStateChanged().connect(
      [this](const QStringList&) { refreshBadges(); });
  auto& downloads = m_core->downloads();
  m_statusId = downloads.downloadItemStatusChanged.connect(
      [this](const DownloadItemStatus& status) { onItemStatus(status); });
  m_finishedId = downloads.downloadFinished.connect(
      [this](const DownloadResult& result) { onDownloadFinished(result); });
  m_busyId = downloads.busyChanged.connect(
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
  auto& downloads = m_core->downloads();
  downloads.downloadItemStatusChanged.disconnect(m_statusId);
  downloads.downloadFinished.disconnect(m_finishedId);
  downloads.busyChanged.disconnect(m_busyId);
  m_core = nullptr;
  m_app = nullptr;
}

void SteamWorkshopUiPlugin::toggleDownloadChecked(const QString& id,
                                                  const QString& title,
                                                  bool checked) {
  batchToggleDownloadChecked({id}, {title}, checked);
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
  batchToggleActive({id}, active);
}

void SteamWorkshopUiPlugin::batchToggleActive(const QStringList& ids,
                                              bool active) {
  if (!m_core) return;
  const QSet<QString> requested(ids.cbegin(), ids.cend());
  auto current = m_core->activeUuids();
  QHash<QString, QString> chosenCopies;
  for (auto it = m_core->allMods().cbegin(); it != m_core->allMods().cend();
       ++it) {
    const auto id = it.value()->publishedFileId();
    if (!id || !requested.contains(*id)) continue;
    if (!active) {
      current.removeAll(it.key());
    } else if (!chosenCopies.contains(*id) || current.contains(it.key())) {
      chosenCopies.insert(*id, it.key());
    }
  }
  if (active) {
    for (const auto& id : ids) {
      const auto copy = chosenCopies.constFind(id);
      if (copy != chosenCopies.cend() && !current.contains(*copy))
        current.append(*copy);
    }
  }
  m_core->setActive(std::move(current));
}

QCoro::Task<void> SteamWorkshopUiPlugin::requestDownload() {
  if (m_checkedIds.isEmpty() || !m_core) co_return;
  if (m_core->config().paths.local.empty()) {
    m_core->downloads().statusMessageChanged.notify(
        QStringLiteral("Local mods path is not configured"));
    co_return;
  }
  if (m_service->isDownloading()) {
    m_core->downloads().statusMessageChanged.notify(
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
  if (!result && m_core)
    m_core->downloads().statusMessageChanged.notify(result.error().message);
}

void SteamWorkshopUiPlugin::stopDownload() { m_service->cancel(); }
bool SteamWorkshopUiPlugin::isDownloading() const {
  return m_service->isDownloading();
}

void SteamWorkshopUiPlugin::dropDownload(const QString& id) {
  m_checkedIds.removeIf([&id](const auto& item) { return item.first == id; });
  m_statuses.remove(id);
}

void SteamWorkshopUiPlugin::removeItem(const QString& id) {
  dropDownload(id);
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

void SteamWorkshopUiPlugin::refreshBadges() {
  refreshCachedIds();
  badgesRefreshRequested.notify(m_installedIds.values());
  activeRefreshRequested.notify(m_activeIds.values());
}

void SteamWorkshopUiPlugin::syncAll() {
  refreshBadges();
  notifySidebar();
}

void SteamWorkshopUiPlugin::onItemStatus(const DownloadItemStatus& status) {
  if (!m_ownDownload) return;
  m_statuses.insert(status.modId, status.status);
  itemStatusChanged.notify(status);
}

void SteamWorkshopUiPlugin::onDownloadFinished(const DownloadResult& result) {
  if (!m_ownDownload) return;
  for (const auto& id : result.succeeded) dropDownload(id);
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
  auto ids = publishedIds(*m_core);
  m_installedIds = std::move(ids.installed);
  m_activeIds = std::move(ids.active);
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
