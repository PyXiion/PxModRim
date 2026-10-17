#include "core/workshop/catalog.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <utility>
#include <variant>

#include "core/context.hpp"
#include "core/mod_service.hpp"

namespace px::modrim::workshop {

namespace {

// Memory keeps the newest responses; the disk keeps the newest cache files.
constexpr qsizetype kCacheLimit = 200;
constexpr qsizetype kDiskLimit = 300;

QStringList toQstrings(const std::vector<std::string>& values) {
  QStringList out;
  out.reserve(static_cast<qsizetype>(values.size()));
  for (const std::string& value : values) {
    out.append(QString::fromStdString(value));
  }
  return out;
}

const std::string& titleOf(const CatalogEntry& entry) {
  return std::visit(
      [](const auto& item) -> const std::string& { return item.title; }, entry);
}

}  // namespace

namespace detail {

Result<void> writeCached(const QString& directory, const QString& path,
                         const std::string& text) {
  if (auto saved = writeFileAtomically(path, text); !saved) {
    return saved;
  }
  QFileInfoList files =
      QDir(directory).entryInfoList({QStringLiteral("*.json")}, QDir::Files);
  std::ranges::sort(files, [](const QFileInfo& a, const QFileInfo& b) {
    return a.lastModified() < b.lastModified();
  });
  for (qsizetype i = 0; i + kDiskLimit < files.size(); ++i) {
    QFile::remove(files[i].absoluteFilePath());
  }
  return {};
}

}  // namespace detail

WorkshopCatalog::WorkshopCatalog()
    : Plugin(QStringLiteral("workshop_catalog"), {QStringLiteral("downloads")}),
      m_settings(nullptr, QStringLiteral("workshop_catalog")),
      m_baseUrl(
          normalizeBaseUrl(QString::fromStdString(m_settings.value().url))),
      m_favourites(nullptr) {
  m_settings.changed.connect([this](const CatalogSettings& value) {
    changeUrl(QString::fromStdString(value.url));
  });
}

void WorkshopCatalog::setup(CoreContext& ctx) {
  m_context = &ctx;
  m_installedMods = [&ctx] { return ctx.allMods().values(); };
  if (ctx.hasConfigService()) {
    m_settings = PluginConfig<CatalogSettings>(
        &ctx.configService(), QStringLiteral("workshop_catalog"),
        {{QStringLiteral("workshop_catalog_url"), QStringLiteral("url")}});
    m_settings.changed.connect([this](const CatalogSettings& value) {
      changeUrl(QString::fromStdString(value.url));
    });
    changeUrl(QString::fromStdString(m_settings.value().url));
    m_cacheDir =
        ctx.configService().directory() + QStringLiteral("/workshop-cache");
    m_favourites = FavouriteCollections(&ctx.configService());
  }
  bindDownloads(ctx.downloads());
  m_modsChangedId = ctx.modService().modsChanged.connect(
      [this](const std::monostate&) { invalidateInstalled(); });
  m_configChangedId = ctx.configChanged().connect(
      [this](const AppConfig&) { invalidateInstalled(); });
}

WorkshopCatalog::WorkshopCatalog(ConfigService* config,
                                 DownloadManager& downloads,
                                 InstalledModsProvider installedMods)
    : Plugin(QStringLiteral("workshop_catalog"), {QStringLiteral("downloads")}),
      m_settings(
          config, QStringLiteral("workshop_catalog"),
          {{QStringLiteral("workshop_catalog_url"), QStringLiteral("url")}}),
      m_baseUrl(
          normalizeBaseUrl(QString::fromStdString(m_settings.value().url))),
      m_favourites(config),
      m_downloads(&downloads),
      m_installedMods(std::move(installedMods)) {
  if (config) {
    m_cacheDir = config->directory() + QStringLiteral("/workshop-cache");
  }
  m_settings.changed.connect([this](const CatalogSettings& settings) {
    changeUrl(QString::fromStdString(settings.url));
  });
  bindDownloads(downloads);
}

void WorkshopCatalog::bindDownloads(DownloadManager& downloads) {
  m_downloads = &downloads;
  m_downloadFinishedId = m_downloads->downloadFinished.connect(
      [this](const DownloadResult&) { invalidateInstalled(); });
  m_busyChangedId = m_downloads->busyChanged.connect([this](bool busy) {
    if (!busy) {
      resumeIdleWaiters();
    }
  });
}

void WorkshopCatalog::disconnectEvents() {
  if (m_downloads) {
    m_downloads->downloadFinished.disconnect(m_downloadFinishedId);
    m_downloads->busyChanged.disconnect(m_busyChangedId);
    m_downloadFinishedId = 0;
    m_busyChangedId = 0;
  }
  if (m_context) {
    m_context->modService().modsChanged.disconnect(m_modsChangedId);
    m_context->configChanged().disconnect(m_configChangedId);
    m_context = nullptr;
  }
}

WorkshopCatalog::~WorkshopCatalog() {
  *m_alive = false;
  disconnectEvents();
  resumeIdleWaiters();
}

QCoro::Task<void> WorkshopCatalog::shutdown() {
  m_stopping = true;
  disconnectEvents();
  cancelQueue();
  auto runner = std::move(m_runner);
  m_runner.reset();
  if (m_downloading) {
    m_downloads->cancel();
  }
  resumeIdleWaiters();
  for (const auto& client : m_clients) {
    client->shutdown();
  }
  if (runner) {
    co_await *runner;
  }
}

void WorkshopCatalog::setBaseUrl(const QString& url) {
  m_settings.update(
      CatalogSettings{.url = normalizeBaseUrl(url).toStdString()});
}

void WorkshopCatalog::changeUrl(const QString& url) {
  const QString normalized = normalizeBaseUrl(url);
  if (normalized == m_baseUrl) {
    return;
  }
  m_baseUrl = normalized;
  m_client = nullptr;
  catalogUrlChanged.notify(m_baseUrl);
}

CatalogClient& WorkshopCatalog::client() {
  if (!m_client) {
    m_clients.push_back(std::make_unique<CatalogClient>(m_baseUrl));
    m_client = m_clients.back().get();
  }
  return *m_client;
}

QString WorkshopCatalog::cacheFile(const QString& key) const {
  const QByteArray digest =
      QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256)
          .toHex();
  return *m_cacheDir + QLatin1Char('/') + QString::fromLatin1(digest.left(32)) +
         QStringLiteral(".json");
}

void WorkshopCatalog::remember(const QString& key, std::any value,
                               bool moveToEnd) {
  if (moveToEnd || !m_responses.contains(key)) {
    m_responseOrder.removeAll(key);
    m_responseOrder.append(key);
  }
  m_responses.insert(key, std::move(value));
  while (m_responseOrder.size() > kCacheLimit) {
    m_responses.remove(m_responseOrder.first());
    m_responseOrder.removeFirst();
  }
}

QCoro::Task<CatalogResult<Discover>> WorkshopCatalog::discover() {
  co_return co_await client().discover();
}

QCoro::Task<CatalogResult<CatalogPage<CatalogMod>>> WorkshopCatalog::mods(
    CatalogQuery query) {
  co_return co_await client().mods(std::move(query));
}

QCoro::Task<CatalogResult<CatalogPage<CatalogCollection>>>
WorkshopCatalog::collections(CatalogQuery query) {
  co_return co_await client().collections(std::move(query));
}

QCoro::Task<CatalogResult<std::optional<CatalogMod>>> WorkshopCatalog::mod(
    QString id) {
  co_return co_await client().mod(std::move(id));
}

QCoro::Task<CatalogResult<std::optional<CollectionDetail>>>
WorkshopCatalog::collection(QString id) {
  co_return co_await client().collection(std::move(id));
}

const WorkshopCatalog::InstalledIndex& WorkshopCatalog::installed() {
  if (!m_installedIndex) {
    InstalledIndex index;
    for (const ListedModPtr& mod : m_installedMods()) {
      if (auto id = mod->publishedFileId()) {
        index[*id].append(mod);
      }
    }
    m_installedIndex = std::move(index);
  }
  return *m_installedIndex;
}

InstallState WorkshopCatalog::installStateOf(
    const CatalogMod& mod, const InstalledIndex& index) const {
  const QList<ListedModPtr> local = index.value(QString::fromStdString(mod.id));
  if (local.isEmpty()) {
    return InstallState::kMissing;
  }
  // Only copies downloaded through the manager have a sync time; a copy without
  // one is never outdated.
  std::optional<double> newestSync;
  for (const ListedModPtr& item : local) {
    if (auto synced = m_downloads->lastSynced(*item)) {
      newestSync = newestSync ? std::max(*newestSync, *synced) : *synced;
    }
  }
  if (newestSync && mod.updated_at &&
      *newestSync < static_cast<double>(*mod.updated_at)) {
    return InstallState::kOutdated;
  }
  return InstallState::kInstalled;
}

InstallState WorkshopCatalog::installState(const CatalogMod& mod) {
  return installStateOf(mod, installed());
}

void WorkshopCatalog::invalidateInstalled() {
  m_installedIndex.reset();
  installedChanged.notify(std::monostate{});
}

QCoro::Task<CatalogResult<std::vector<CatalogMod>>>
WorkshopCatalog::installedWithUpdates() {
  QStringList ids = installed().keys();
  // The index is a hash, so sort the ids to request them in a stable order.
  ids.sort();
  auto batch = co_await client().modsBatch(std::move(ids));
  if (!batch) {
    co_return std::unexpected(batch.error());
  }
  co_return std::move(batch->items);
}

QCoro::Task<CatalogResult<DownloadPlan>> WorkshopCatalog::plan(
    QStringList modIds, QStringList collectionIds) {
  auto resolved =
      co_await client().resolve(std::move(modIds), std::move(collectionIds));
  if (!resolved) {
    co_return std::unexpected(resolved.error());
  }
  const InstalledIndex& index = installed();
  DownloadPlan result;
  for (const std::string& id : resolved->mod_ids) {
    const auto found = resolved->items.find(id);
    const auto* mod = found == resolved->items.end()
                          ? nullptr
                          : std::get_if<CatalogMod>(&found->second);
    if (!mod) {
      co_return std::unexpected(CatalogError{
          QStringLiteral(
              "The Workshop catalog returned an invalid download plan."),
          std::nullopt});
    }
    const QString qid = QString::fromStdString(id);
    if (installStateOf(*mod, index) == InstallState::kInstalled) {
      result.already_current.append(qid);
    } else {
      result.to_download.append(qid);
    }
  }
  for (const auto& [id, entry] : resolved->items) {
    result.titles.insert(QString::fromStdString(id),
                         QString::fromStdString(titleOf(entry)));
  }
  result.unavailable_ids = toQstrings(resolved->unavailable_ids);
  result.incomplete_collection_ids =
      toQstrings(resolved->incomplete_collection_ids);
  result.is_complete = resolved->is_complete;
  co_return result;
}

QCoro::Task<Result<DownloadResult>> WorkshopCatalog::download(
    DownloadPlan downloadPlan) {
  co_return co_await m_downloads->downloadMods(
      std::move(downloadPlan.to_download));
}

void WorkshopCatalog::enqueue(const DownloadPlan& downloadPlan) {
  if (m_stopping) {
    return;
  }
  const QSet<QString> active = m_downloads->activeIds();
  QStringList ids;
  for (const QString& id : downloadPlan.to_download) {
    if (!m_queued.contains(id) && !active.contains(id)) {
      ids.append(id);
    }
  }
  if (ids.isEmpty()) {
    return;
  }
  m_queue.append(ids);
  m_queued.unite(QSet<QString>(ids.begin(), ids.end()));
  queueChanged.notify(std::monostate{});
  if (!m_draining) {
    m_draining = true;
    m_runner.emplace(drain());
  }
}

void WorkshopCatalog::cancelQueue() {
  m_queue.clear();
  m_queued.clear();
  queueChanged.notify(std::monostate{});
}

QCoro::Task<void> WorkshopCatalog::drain() {
  const auto alive = m_alive;
  while (!m_queue.isEmpty()) {
    const QStringList ids = m_queue.takeFirst();
    // A batch that waited for the running download stops the queue if that
    // download was cancelled.
    const bool waited = m_downloads->isDownloading();
    while (m_downloads->isDownloading()) {
      co_await IdleWait{*this};
      if (!*alive) {
        co_return;
      }
      if (m_stopping) {
        m_draining = false;
        co_return;
      }
    }
    if (waited && m_downloads->cancelled()) {
      cancelQueue();
      break;
    }

    m_downloading = true;
    auto result = co_await m_downloads->downloadMods(ids);
    if (!*alive) {
      co_return;
    }
    m_downloading = false;
    if (!result) {
      spdlog::warn("[workshop] queued download failed: {}",
                   result.error().message.toStdString());
    }
    m_queued.subtract(QSet<QString>(ids.begin(), ids.end()));
    queueChanged.notify(std::monostate{});
    if (m_downloads->cancelled()) {
      cancelQueue();
      break;
    }
  }
  m_draining = false;
}

void WorkshopCatalog::resumeIdleWaiters() {
  auto waiters = std::move(m_idleWaiters);
  m_idleWaiters.clear();
  for (std::coroutine_handle<> waiter : waiters) {
    waiter.resume();
  }
}

}  // namespace px::modrim::workshop
