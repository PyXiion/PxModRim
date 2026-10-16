#include "ui/plugins/steam_workshop/plugin.h"

#ifdef PX_HAS_STEAM

#include <algorithm>
#include <chrono>

#include <QCoro/QCoroNetworkReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

#include "core/mod_service.h"
#include "ui/navigation.h"
#include "ui/plugins/steam_workshop/view.h"

namespace px::modrim {

SteamWorkshopUiPlugin::SteamWorkshopUiPlugin()
    : UiPlugin(QStringLiteral("steamworkshop"), {QStringLiteral("steam_downloader")}) {}
SteamWorkshopUiPlugin::~SteamWorkshopUiPlugin() { disconnect_events(); }

void SteamWorkshopUiPlugin::setup(AppContext& ctx) {
    core_ = &ctx.coreContext();
    app_ = &ctx;
    service_ = static_cast<SteamDownloader*>(core_->plugins().get(QStringLiteral("steam_downloader")));
    ctx.addRailView<SteamWorkshopViewPanel>();
    mods_id_ = core_->modService().mods_changed.connect([this](const std::monostate&) {
        refresh_cached_ids();
        badges_refresh_requested.notify(installed_ids_.values());
        active_refresh_requested.notify(active_ids_.values());
    });
    active_id_ = core_->activeStateChanged().connect([this](const QStringList&) {
        refresh_cached_ids();
        badges_refresh_requested.notify(installed_ids_.values());
        active_refresh_requested.notify(active_ids_.values());
    });
    status_id_ = service_->download_item_status_changed.connect([this](const DownloadItemStatus& status) { on_item_status(status); });
    finished_id_ = service_->download_finished.connect([this](const DownloadResult& result) { on_download_finished(result); });
    busy_id_ = service_->busy_changed.connect([this](bool busy) { download_busy_changed.notify(busy); });
}

QCoro::Task<void> SteamWorkshopUiPlugin::init(AppContext&) {
    refresh_cached_ids();
    co_return;
}

QCoro::Task<void> SteamWorkshopUiPlugin::shutdown() {
    disconnect_events();
    co_return;
}

void SteamWorkshopUiPlugin::disconnect_events() {
    if (!core_) return;
    core_->modService().mods_changed.disconnect(mods_id_);
    core_->activeStateChanged().disconnect(active_id_);
    service_->download_item_status_changed.disconnect(status_id_);
    service_->download_finished.disconnect(finished_id_);
    service_->busy_changed.disconnect(busy_id_);
    core_ = nullptr;
    app_ = nullptr;
}

void SteamWorkshopUiPlugin::toggle_download_checked(const QString& id, const QString& title, bool checked) {
    const auto it = std::ranges::find(checked_ids_, id, &CheckedDownloads::value_type::first);
    if (checked) {
        if (it == checked_ids_.end()) checked_ids_.append({id, title});
        else it->second = title;
    } else if (it != checked_ids_.end()) {
        checked_ids_.erase(it);
    }
    notify_sidebar();
}

void SteamWorkshopUiPlugin::batch_toggle_download_checked(const QStringList& ids, const QStringList& titles, bool checked) {
    for (qsizetype pos = 0; pos < ids.size(); ++pos) {
        const auto it = std::ranges::find(checked_ids_, ids[pos], &CheckedDownloads::value_type::first);
        if (checked) {
            if (it == checked_ids_.end()) checked_ids_.append({ids[pos], titles[pos]});
            else it->second = titles[pos];
        } else if (it != checked_ids_.end()) {
            checked_ids_.erase(it);
        }
    }
    if (!ids.isEmpty()) notify_sidebar();
}

void SteamWorkshopUiPlugin::toggle_active(const QString& id, bool active) {
    if (!core_) return;
    for (auto it = core_->allMods().cbegin(); it != core_->allMods().cend(); ++it) {
        if (it.value()->published_file_id() != id) continue;
        auto current = core_->activeUuids();
        if (active) {
            if (!current.contains(it.key())) current.append(it.key());
        } else {
            current.removeAll(it.key());
        }
        core_->setActive(std::move(current));
        return;
    }
}

void SteamWorkshopUiPlugin::batch_toggle_active(const QStringList& ids, bool active) {
    if (!core_) return;
    QHash<QString, QString> by_published_id;
    for (auto it = core_->allMods().cbegin(); it != core_->allMods().cend(); ++it) {
        if (auto id = it.value()->published_file_id()) by_published_id.insert(*id, it.key());
    }
    auto current = core_->activeUuids();
    for (const auto& id : ids) {
        const auto it = by_published_id.constFind(id);
        if (it == by_published_id.cend()) continue;
        if (active) {
            if (!current.contains(*it)) current.append(*it);
        } else {
            current.removeAll(*it);
        }
    }
    core_->setActive(std::move(current));
}

QCoro::Task<void> SteamWorkshopUiPlugin::request_download() {
    if (checked_ids_.isEmpty()) co_return;
    if (!core_ || core_->config().paths.local.empty()) {
        service_->status_message_changed.notify(QStringLiteral("Local mods path is not configured"));
        co_return;
    }
    if (service_->is_downloading()) {
        service_->status_message_changed.notify(QStringLiteral("A download is already running"));
        co_return;
    }
    QStringList ids;
    ids.reserve(checked_ids_.size());
    for (const auto& [id, title] : checked_ids_) {
        ids.append(id);
        statuses_.insert(id, QStringLiteral("queued"));
    }
    own_download_ = true;
    if (app_) app_->navigate(buildRoute(QStringLiteral("downloads")));
    notify_sidebar();
    auto result = co_await service_->download_mods(std::move(ids));
    own_download_ = false;
    if (!result) service_->status_message_changed.notify(result.error().message);
}

void SteamWorkshopUiPlugin::stop_download() { service_->cancel(); }
bool SteamWorkshopUiPlugin::is_downloading() const { return service_->is_downloading(); }

void SteamWorkshopUiPlugin::remove_item(const QString& id) {
    checked_ids_.removeIf([&id](const auto& item) { return item.first == id; });
    statuses_.remove(id);
    notify_sidebar();
    uncheck_mod_requested.notify(id);
}

void SteamWorkshopUiPlugin::clear_queue() {
    checked_ids_.clear();
    statuses_.clear();
    notify_sidebar();
    clear_checked_requested.notify(std::monostate{});
}

void SteamWorkshopUiPlugin::notify_sidebar() { sidebar_sync_requested.notify(SidebarSync{checked_ids_, statuses_}); }

void SteamWorkshopUiPlugin::sync_all() {
    refresh_cached_ids();
    badges_refresh_requested.notify(installed_ids_.values());
    active_refresh_requested.notify(active_ids_.values());
    notify_sidebar();
}

void SteamWorkshopUiPlugin::on_item_status(const DownloadItemStatus& status) {
    if (!own_download_) return;
    statuses_.insert(status.mod_id, status.status);
    item_status_changed.notify(status);
}

void SteamWorkshopUiPlugin::on_download_finished(const DownloadResult& result) {
    if (!own_download_) return;
    for (const auto& id : result.succeeded) {
        checked_ids_.removeIf([&id](const auto& item) { return item.first == id; });
        statuses_.remove(id);
    }
    for (auto it = statuses_.begin(); it != statuses_.end();) {
        if (it.value() == QLatin1String("queued") || it.value() == QLatin1String("downloading")) it = statuses_.erase(it);
        else ++it;
    }
    notify_sidebar();
}

void SteamWorkshopUiPlugin::refresh_cached_ids() {
    if (!core_) return;
    installed_ids_.clear();
    active_ids_.clear();
    const QSet<QString> active(core_->activeUuids().cbegin(), core_->activeUuids().cend());
    for (const auto& mod : core_->allMods()) {
        if (auto id = mod->published_file_id()) {
            installed_ids_.insert(*id);
            if (active.contains(mod->uuid())) active_ids_.insert(*id);
        }
    }
}

QCoro::Task<Result<QJsonValue>> SteamWorkshopUiPlugin::fetch_mod_deps(QString id) {
    QUrl url(QStringLiteral("https://deps.modrim.pyxiion.ru/deps"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"), id);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "PxModRim/1.0");
    request.setTransferTimeout(30000);
    auto* reply = core_->network().get(request);
    if (!co_await qCoro(reply).waitForFinished(std::chrono::seconds{30})) {
        reply->abort();
        reply->deleteLater();
        co_return std::unexpected(Error{QStringLiteral("Timeout")});
    }
    const auto error = reply->error();
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto bytes = reply->readAll();
    const auto message = reply->errorString();
    reply->deleteLater();
    if (error == QNetworkReply::TimeoutError) co_return std::unexpected(Error{QStringLiteral("Timeout")});
    if (status && status != 200) co_return QJsonValue(QJsonValue::Null);
    if (error != QNetworkReply::NoError) co_return std::unexpected(Error{message});
    if (bytes.isEmpty()) co_return QJsonValue(QJsonValue::Null);
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(QByteArray("[") + bytes + ']', &parse_error);
    if (parse_error.error != QJsonParseError::NoError) co_return std::unexpected(Error{parse_error.errorString()});
    co_return document.array().at(0);
}

}  // namespace px::modrim
#endif
