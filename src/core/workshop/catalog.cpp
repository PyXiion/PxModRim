#include "core/workshop/catalog.hpp"

#include <algorithm>
#include <utility>
#include <variant>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "core/context.hpp"
#include "core/mod_service.hpp"

namespace px::modrim::workshop {

namespace {

// Memory keeps the newest responses; the disk keeps the newest cache files.
constexpr qsizetype kCacheLimit = 200;
constexpr qsizetype kDiskLimit = 300;

QStringList to_qstrings(const std::vector<std::string>& values) {
    QStringList out;
    out.reserve(static_cast<qsizetype>(values.size()));
    for (const std::string& value : values) {
        out.append(QString::fromStdString(value));
    }
    return out;
}

const std::string& title_of(const CatalogEntry& entry) {
    return std::visit([](const auto& item) -> const std::string& { return item.title; }, entry);
}

}  // namespace

namespace detail {

Result<void> write_cached(const QString& directory, const QString& path, const std::string& text) {
    if (auto saved = write_file_atomically(path, text); !saved) {
        return saved;
    }
    QFileInfoList files = QDir(directory).entryInfoList({QStringLiteral("*.json")}, QDir::Files);
    std::ranges::sort(files, [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() < b.lastModified(); });
    for (qsizetype i = 0; i + kDiskLimit < files.size(); ++i) {
        QFile::remove(files[i].absoluteFilePath());
    }
    return {};
}

}  // namespace detail

WorkshopCatalog::WorkshopCatalog()
    : Plugin(QStringLiteral("workshop_catalog"), {QStringLiteral("downloads")}),
      settings_(nullptr, QStringLiteral("workshop_catalog")),
      base_url_(normalize_base_url(QString::fromStdString(settings_.value().url))), favourites_(nullptr) {
    settings_.changed.connect([this](const CatalogSettings& value) { change_url(QString::fromStdString(value.url)); });
}

void WorkshopCatalog::setup(CoreContext& ctx) {
    context_ = &ctx;
    installed_mods_ = [&ctx] { return ctx.allMods().values(); };
    if (ctx.hasConfigService()) {
        settings_ = PluginConfig<CatalogSettings>(&ctx.configService(), QStringLiteral("workshop_catalog"),
            {{QStringLiteral("workshop_catalog_url"), QStringLiteral("url")}});
        settings_.changed.connect([this](const CatalogSettings& value) { change_url(QString::fromStdString(value.url)); });
        change_url(QString::fromStdString(settings_.value().url));
        cache_dir_ = ctx.configService().directory() + QStringLiteral("/workshop-cache");
        favourites_ = FavouriteCollections(&ctx.configService());
    }
    bind_downloads(ctx.downloads());
    mods_changed_id_ = ctx.modService().mods_changed.connect([this](const std::monostate&) { invalidate_installed(); });
    config_changed_id_ = ctx.configChanged().connect([this](const AppConfig&) { invalidate_installed(); });
}

WorkshopCatalog::WorkshopCatalog(ConfigService* config, DownloadManager& downloads,
                                 InstalledModsProvider installed_mods)
    : Plugin(QStringLiteral("workshop_catalog"), {QStringLiteral("downloads")}),
      settings_(config, QStringLiteral("workshop_catalog"),
                {{QStringLiteral("workshop_catalog_url"), QStringLiteral("url")}}),
      base_url_(normalize_base_url(QString::fromStdString(settings_.value().url))),
      favourites_(config),
      downloads_(&downloads),
      installed_mods_(std::move(installed_mods)) {
    if (config) {
        cache_dir_ = config->directory() + QStringLiteral("/workshop-cache");
    }
    settings_.changed.connect(
        [this](const CatalogSettings& settings) { change_url(QString::fromStdString(settings.url)); });
    bind_downloads(downloads);
}

void WorkshopCatalog::bind_downloads(DownloadManager& downloads) {
    downloads_ = &downloads;
    download_finished_id_ = downloads_->download_finished.connect([this](const DownloadResult&) { invalidate_installed(); });
    busy_changed_id_ = downloads_->busy_changed.connect([this](bool busy) { on_busy_changed(busy); });
}

void WorkshopCatalog::disconnect_events() {
    if (downloads_) {
        downloads_->download_finished.disconnect(download_finished_id_);
        downloads_->busy_changed.disconnect(busy_changed_id_);
        download_finished_id_ = 0;
        busy_changed_id_ = 0;
    }
    if (context_) {
        context_->modService().mods_changed.disconnect(mods_changed_id_);
        context_->configChanged().disconnect(config_changed_id_);
        context_ = nullptr;
    }
}

WorkshopCatalog::~WorkshopCatalog() {
    disconnect_events();
    // Destroying the queue's coroutine must not leave the downloads holding its handle.
    idle_waiters_.clear();
    runner_.reset();
}

QCoro::Task<void> WorkshopCatalog::shutdown() {
    disconnect_events();
    cancel_queue();
    idle_waiters_.clear();
    runner_.reset();
    draining_ = false;
    for (const auto& client : clients_) {
        client->shutdown();
    }
    co_return;
}

void WorkshopCatalog::set_base_url(const QString& url) {
    settings_.update(CatalogSettings{.url = normalize_base_url(url).toStdString()});
}

void WorkshopCatalog::change_url(const QString& url) {
    const QString normalized = normalize_base_url(url);
    if (normalized == base_url_) {
        return;
    }
    base_url_ = normalized;
    client_ = nullptr;
    catalog_url_changed.notify(base_url_);
}

CatalogClient& WorkshopCatalog::client() {
    if (!client_) {
        clients_.push_back(std::make_unique<CatalogClient>(base_url_));
        client_ = clients_.back().get();
    }
    return *client_;
}

QString WorkshopCatalog::cache_file(const QString& key) const {
    const QByteArray digest = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex();
    return *cache_dir_ + QLatin1Char('/') + QString::fromLatin1(digest.left(32)) + QStringLiteral(".json");
}

void WorkshopCatalog::remember(const QString& key, std::any value, bool move_to_end) {
    if (move_to_end || !responses_.contains(key)) {
        response_order_.removeAll(key);
        response_order_.append(key);
    }
    responses_.insert(key, std::move(value));
    while (response_order_.size() > kCacheLimit) {
        responses_.remove(response_order_.first());
        response_order_.removeFirst();
    }
}

QCoro::Task<CatalogResult<Discover>> WorkshopCatalog::discover() {
    co_return co_await client().discover();
}

QCoro::Task<CatalogResult<CatalogPage<CatalogMod>>> WorkshopCatalog::mods(CatalogQuery query) {
    co_return co_await client().mods(std::move(query));
}

QCoro::Task<CatalogResult<CatalogPage<CatalogCollection>>> WorkshopCatalog::collections(CatalogQuery query) {
    co_return co_await client().collections(std::move(query));
}

QCoro::Task<CatalogResult<std::optional<CatalogMod>>> WorkshopCatalog::mod(QString id) {
    co_return co_await client().mod(std::move(id));
}

QCoro::Task<CatalogResult<std::optional<CollectionDetail>>> WorkshopCatalog::collection(QString id) {
    co_return co_await client().collection(std::move(id));
}

const WorkshopCatalog::InstalledIndex& WorkshopCatalog::installed() {
    if (!installed_index_) {
        InstalledIndex index;
        for (const ListedModPtr& mod : installed_mods_()) {
            if (auto id = mod->published_file_id()) {
                index[*id].append(mod);
            }
        }
        installed_index_ = std::move(index);
    }
    return *installed_index_;
}

InstallState WorkshopCatalog::install_state_of(const CatalogMod& mod, const InstalledIndex& index) const {
    const QList<ListedModPtr> local = index.value(QString::fromStdString(mod.id));
    if (local.isEmpty()) {
        return InstallState::missing;
    }
    // Only copies downloaded through the manager have a sync time; a copy without one is never outdated.
    std::optional<double> newest_sync;
    for (const ListedModPtr& item : local) {
        if (auto synced = downloads_->last_synced(*item)) {
            newest_sync = newest_sync ? std::max(*newest_sync, *synced) : *synced;
        }
    }
    if (newest_sync && mod.updated_at && *newest_sync < static_cast<double>(*mod.updated_at)) {
        return InstallState::outdated;
    }
    return InstallState::installed;
}

InstallState WorkshopCatalog::install_state(const CatalogMod& mod) {
    return install_state_of(mod, installed());
}

void WorkshopCatalog::invalidate_installed() {
    installed_index_.reset();
    installed_changed.notify(std::monostate{});
}

QCoro::Task<CatalogResult<std::vector<CatalogMod>>> WorkshopCatalog::installed_with_updates() {
    QStringList ids = installed().keys();
    // The index is a hash, so sort the ids to request them in a stable order.
    ids.sort();
    auto batch = co_await client().mods_batch(std::move(ids));
    if (!batch) {
        co_return std::unexpected(batch.error());
    }
    co_return std::move(batch->items);
}

QCoro::Task<CatalogResult<DownloadPlan>> WorkshopCatalog::plan(QStringList mod_ids, QStringList collection_ids) {
    auto resolved = co_await client().resolve(std::move(mod_ids), std::move(collection_ids));
    if (!resolved) {
        co_return std::unexpected(resolved.error());
    }
    const InstalledIndex& index = installed();
    DownloadPlan result;
    for (const std::string& id : resolved->mod_ids) {
        const auto found = resolved->items.find(id);
        const auto* mod = found == resolved->items.end() ? nullptr : std::get_if<CatalogMod>(&found->second);
        if (!mod) {
            co_return std::unexpected(CatalogError{
                QStringLiteral("The Workshop catalog returned an invalid download plan."), std::nullopt});
        }
        const QString qid = QString::fromStdString(id);
        if (install_state_of(*mod, index) == InstallState::installed) {
            result.already_current.append(qid);
        } else {
            result.to_download.append(qid);
        }
    }
    for (const auto& [id, entry] : resolved->items) {
        result.titles.insert(QString::fromStdString(id), QString::fromStdString(title_of(entry)));
    }
    result.unavailable_ids = to_qstrings(resolved->unavailable_ids);
    result.incomplete_collection_ids = to_qstrings(resolved->incomplete_collection_ids);
    result.is_complete = resolved->is_complete;
    co_return result;
}

QCoro::Task<Result<DownloadResult>> WorkshopCatalog::download(DownloadPlan download_plan) {
    co_return co_await downloads_->download_mods(std::move(download_plan.to_download));
}

void WorkshopCatalog::enqueue(const DownloadPlan& download_plan) {
    const QSet<QString> active = downloads_->active_ids();
    QStringList ids;
    for (const QString& id : download_plan.to_download) {
        if (!queued_.contains(id) && !active.contains(id)) {
            ids.append(id);
        }
    }
    if (ids.isEmpty()) {
        return;
    }
    queue_.append(ids);
    queued_.unite(QSet<QString>(ids.begin(), ids.end()));
    queue_changed.notify(std::monostate{});
    if (!draining_) {
        draining_ = true;
        runner_.emplace(drain());
    }
}

void WorkshopCatalog::cancel_queue() {
    queue_.clear();
    queued_.clear();
    queue_changed.notify(std::monostate{});
}

QCoro::Task<void> WorkshopCatalog::drain() {
    while (!queue_.isEmpty()) {
        const QStringList ids = queue_.takeFirst();
        // A batch that waited for the running download stops the queue if that download was cancelled.
        const bool waited = downloads_->is_downloading();
        while (downloads_->is_downloading()) {
            co_await IdleWait{*this};
        }
        if (waited && downloads_->cancelled()) {
            cancel_queue();
            break;
        }

        auto result = co_await downloads_->download_mods(ids);
        if (!result) {
            spdlog::warn("[workshop] queued download failed: {}", result.error().message.toStdString());
        }
        queued_.subtract(QSet<QString>(ids.begin(), ids.end()));
        queue_changed.notify(std::monostate{});
        if (downloads_->cancelled()) {
            cancel_queue();
            break;
        }
    }
    draining_ = false;
}

void WorkshopCatalog::on_busy_changed(bool busy) {
    if (busy) {
        return;
    }
    auto waiters = std::move(idle_waiters_);
    idle_waiters_.clear();
    for (std::coroutine_handle<> waiter : waiters) {
        waiter.resume();
    }
}

}  // namespace px::modrim::workshop
