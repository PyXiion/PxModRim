#ifdef PX_HAS_STEAM

#include "core/downloads/steam.hpp"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QPromise>
#include <QScopeGuard>
#include <QtConcurrent>
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <QCoro/QCoroFuture>

#include <spdlog/spdlog.h>

#include "core/utils.hpp"

namespace px::modrim {
namespace {

constexpr auto kProgressLogInterval = std::chrono::seconds(15);
constexpr auto kProgressEmitInterval = std::chrono::milliseconds(100);

// Steam item ids are uint64; anything larger is not an item.
std::optional<std::uint64_t> parseItemId(const QString& text) {
  if (!isAsciiDigits(text)) {
    return std::nullopt;
  }
  const std::string utf8 = text.toStdString();
  std::uint64_t value = 0;
  const char* const last = utf8.data() + utf8.size();
  const auto [end, ec] = std::from_chars(utf8.data(), last, value);
  if (ec != std::errc{} || end != last) {
    return std::nullopt;
  }
  return value;
}

QString itemIdText(std::uint64_t id) {
  return QString::number(static_cast<qulonglong>(id));
}

double unixNow() {
  return std::chrono::duration<double>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// Settings used to live in config.json under `workshop_<field>` keys.
QHash<QString, QString> legacySettingsKeys() {
  QHash<QString, QString> keys;
  for (const char* field :
       {"auto_update_hours", "parallel_items", "threads_per_item", "proxy",
        "connect_timeout", "stall_timeout"}) {
    keys.insert(QStringLiteral("workshop_") + QString::fromLatin1(field),
                QString::fromLatin1(field));
  }
  return keys;
}

// The real client; pxsteamdl::Client is thread-safe, so the batch worker may
// use it directly.
class PxSteamDlClient final : public WorkshopClient {
 public:
  explicit PxSteamDlClient(const pxsteamdl::ClientOptions& options)
      : m_client(options) {}

  std::vector<pxsteamdl::Result> download(
      std::span<const std::uint64_t> ids, const std::filesystem::path& root,
      const pxsteamdl::Options& options) override {
    return m_client.download(ids, root, options);
  }

 private:
  pxsteamdl::Client m_client;
};

}  // namespace

QCoro::Task<Result<std::shared_ptr<WorkshopClient>>> loginWithPxsteamdl(
    LoginOptions options) {
  co_return co_await QtConcurrent::run(
      [options =
           std::move(options)]() -> Result<std::shared_ptr<WorkshopClient>> {
        pxsteamdl::ClientOptions native;
        native.proxy = options.proxy;
        native.connect_timeout = std::chrono::seconds(options.connectTimeout);
        native.stall_timeout = std::chrono::seconds(options.stallTimeout);
        try {
          return std::shared_ptr<WorkshopClient>(
              std::make_shared<PxSteamDlClient>(native));
        } catch (const std::exception& e) {
          return std::unexpected(Error{QString::fromUtf8(e.what())});
        }
      });
}

SteamBatch::SteamBatch(int totalItems)
    : total(totalItems),
      started(std::chrono::steady_clock::now()),
      lastLogged(started) {}

QStringList SteamBatch::changed() const {
  QStringList result;
  for (const auto& id : succeeded) {
    if (bytes.value(id, {0, 0}).second > 0) {
      result.append(id);
    }
  }
  return result;
}

DownloadProgress SteamBatch::progress() const {
  // Items that report all their bytes (or 0/0 for an up-to-date copy) count as
  // done before their Result arrives.
  QSet<QString> finished(succeeded.begin(), succeeded.end());
  finished.unite(QSet<QString>(failed.begin(), failed.end()));
  qint64 bytesDone = 0;
  qint64 bytesTotal = 0;
  for (auto it = bytes.cbegin(); it != bytes.cend(); ++it) {
    const auto [done, size] = it.value();
    if (done >= size) {
      finished.insert(it.key());
    }
    bytesDone += done;
    bytesTotal += size;
  }
  return DownloadProgress{
      .total = total,
      .completed = static_cast<int>(finished.size()),
      .bytesDone = bytesDone,
      .bytesTotal = bytesTotal,
  };
}

SteamDownloader::SteamDownloader(SteamDownloaderDeps deps)
    : Downloader("steam_downloader", "Steam Workshop", deps.manager),
      m_config(deps.config),
      m_localModsPath(std::move(deps.localModsPath)),
      m_installedMods(std::move(deps.installedMods)),
      m_clientFactory(std::move(deps.clientFactory)),
      m_settings(&deps.config, QStringLiteral("steam_downloader"),
                 legacySettingsKeys()) {
  m_autoTimer.setSingleShot(true);
  QObject::connect(&m_autoTimer, &QTimer::timeout, &m_autoTimer,
                   [this] { runScheduledAutoUpdate(); });
}

QCoro::Task<void> SteamDownloader::loadSyncState() {
  ConfigService& config = m_config;
  auto loaded = co_await QtConcurrent::run(
      [&config] { return config.load<WorkshopSyncState>(kWorkshopSyncFile); });
  if (loaded) {
    m_sync = std::move(*loaded);
  } else {
    spdlog::warn("[workshop] cannot read sync times: {}",
                 loaded.error().message.toStdString());
  }
}

QCoro::Task<void> SteamDownloader::init(CoreContext&) {
  co_await loadSyncState();
  armAutoUpdate(kAutoUpdateStartupDelay);
}

QCoro::Task<void> SteamDownloader::shutdown() {
  m_stopping = true;
  m_autoTimer.stop();
  cancel();
  if (m_batchFinished.isValid()) {
    QFuture<void> pending = m_batchFinished;
    co_await pending;
  }
}

bool SteamDownloader::isDownloading() const { return m_stop.has_value(); }

QSet<QString> SteamDownloader::activeIds() const { return m_activeIds; }

bool SteamDownloader::accepts(const QString& modId) const {
  return isAsciiDigits(modId);
}

std::optional<QString> SteamDownloader::updatableId(
    const ListedMod& mod) const {
  const QString local = localPath();
  if (local.isEmpty()) {
    return std::nullopt;
  }
  const auto& path = mod.modPath();
  if (!path) {
    return std::nullopt;
  }
  const QFileInfo info(*path);
  if (QDir::cleanPath(info.path()) != QDir::cleanPath(local)) {
    return std::nullopt;
  }
  const auto pfid = mod.publishedFileId();
  if (!pfid || *pfid != info.fileName()) {
    return std::nullopt;
  }
  return pfid;
}

std::optional<double> SteamDownloader::lastSynced(const ListedMod& mod) const {
  const auto id = updatableId(mod);
  return id ? syncedAt(*id) : std::nullopt;
}

QStringList SteamDownloader::staleIds(double maxAgeS, double now) const {
  const double cutoff = now - maxAgeS;
  const auto syncedOrZero = [this](const QString& id) {
    return syncedAt(id).value_or(0.0);
  };
  QStringList stale;
  for (const auto& id : updatableIds(m_installedMods())) {
    if (syncedOrZero(id) < cutoff) {
      stale.append(id);
    }
  }
  std::stable_sort(stale.begin(), stale.end(),
                   [&](const QString& a, const QString& b) {
                     return syncedOrZero(a) < syncedOrZero(b);
                   });
  return stale;
}

QCoro::Task<Result<DownloadResult>> SteamDownloader::downloadMods(
    QStringList modIds) {
  if (m_stopping) {
    co_return std::unexpected(
        Error{QStringLiteral("Steam downloader is shutting down.")});
  }
  if (modIds.isEmpty()) {
    co_return std::unexpected(
        Error{QStringLiteral("No mods selected for download.")});
  }
  for (const auto& id : modIds) {
    if (!parseItemId(id)) {
      co_return std::unexpected(Error{QStringLiteral(
          "Published file IDs must be ASCII decimal numbers within uint64.")});
    }
  }
  const QString root = localPath();
  if (root.isEmpty()) {
    co_return std::unexpected(
        Error{QStringLiteral("Local mods path is not configured.")});
  }
  if (isDownloading()) {
    co_return std::unexpected(
        Error{QStringLiteral("A download is already running.")});
  }

  QStringList ids;
  QSet<QString> seen;
  for (const auto& id : modIds) {
    if (!seen.contains(id)) {
      seen.insert(id);
      ids.append(id);
    }
  }

  auto batch = std::make_shared<SteamBatch>(static_cast<int>(ids.size()));
  m_stop.emplace();
  QPromise<void> completion;
  completion.start();
  m_batchFinished = completion.future();
  const auto finish = qScopeGuard([&completion] { completion.finish(); });
  m_activeIds = QSet<QString>(ids.begin(), ids.end());
  busyChanged.notify(true);
  batchStarted.notify(ids);
  spdlog::info("[workshop] downloading {} items into {} (first ids: {})",
               ids.size(), root.toStdString(),
               ids.mid(0, 5).join(QStringLiteral(", ")).toStdString());

  try {
    co_await runBatch(ids, root, batch);
  } catch (...) {
    finishBatch(batch);
    throw;
  }
  co_return finishBatch(batch);
}

void SteamDownloader::cancel() {
  if (m_stop) {
    spdlog::debug("[workshop] cancel requested");
    m_stop->request_stop();
  }
}

QString SteamDownloader::localPath() const { return m_localModsPath(); }

std::optional<double> SteamDownloader::syncedAt(const QString& modId) const {
  const auto it = m_sync.synced.find(modId.toStdString());
  if (it == m_sync.synced.end()) {
    return std::nullopt;
  }
  return it->second;
}

LoginOptions SteamDownloader::wantedOptions() const {
  const auto& cfg = m_settings.value();
  LoginOptions options;
  const QString proxy = QString::fromStdString(cfg.proxy).trimmed();
  if (!proxy.isEmpty()) {
    options.proxy = proxy.toStdString();
  }
  options.connectTimeout = std::max(1, cfg.connect_timeout);
  options.stallTimeout = std::max(1, cfg.stall_timeout);
  return options;
}

QCoro::Task<Result<std::shared_ptr<WorkshopClient>>>
SteamDownloader::ensureClient() {
  const LoginOptions options = wantedOptions();
  if (m_client && options != m_clientOptions) {
    m_client.reset();
  }
  if (!m_client) {
    statusMessageChanged.notify(QStringLiteral(u"Logging in to Steam\u2026"));
    auto loggedIn = co_await m_clientFactory(options);
    if (!loggedIn) {
      co_return std::unexpected(loggedIn.error());
    }
    m_client = std::move(*loggedIn);
    m_clientOptions = options;
  }
  auto result = m_client;
  co_return result;
}

QCoro::Task<void> SteamDownloader::runBatch(QStringList ids, QString root,
                                            std::shared_ptr<SteamBatch> batch) {
  const std::stop_token stop = m_stop->get_token();
  const std::filesystem::path rootPath(root.toStdWString());
  const std::error_code mkdirError = co_await QtConcurrent::run([rootPath] {
    std::error_code ec;
    std::filesystem::create_directories(rootPath, ec);
    return ec;
  });
  if (stop.stop_requested()) {
    for (const auto& id : ids) {
      finishItem(*batch, id, QStringLiteral("cancelled"));
    }
    co_return;
  }
  if (mkdirError) {
    const QString message =
        QStringLiteral("Cannot create %1: %2")
            .arg(root, QString::fromStdString(mkdirError.message()));
    spdlog::error("[workshop] {}", message.toStdString());
    statusMessageChanged.notify(message);
    for (const auto& id : ids) {
      finishItem(*batch, id, message);
    }
    co_return;
  }

  if (!m_client) {
    downloadPhaseChanged.notify(QStringLiteral("login"));
  }
  const auto loginStarted = std::chrono::steady_clock::now();
  auto login = co_await ensureClient();
  if (stop.stop_requested()) {
    for (const auto& id : ids) {
      finishItem(*batch, id, QStringLiteral("cancelled"));
    }
    co_return;
  }
  if (!login) {
    const QString message =
        QStringLiteral("Steam login failed: %1").arg(login.error().message);
    spdlog::error("[workshop] {}", message.toStdString());
    statusMessageChanged.notify(message);
    for (const auto& id : ids) {
      finishItem(*batch, id, message);
    }
    co_return;
  }
  spdlog::debug("[workshop] steam session ready in {:.1f}s",
                std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                              loginStarted)
                    .count());
  const std::shared_ptr<WorkshopClient> client = *login;

  const auto& cfg = m_settings.value();
  const int parallelItems =
      std::clamp(cfg.parallel_items, 1, kSteamMaxParallelItems);
  const int threadsPerItem =
      std::clamp(cfg.threads_per_item, 1, kSteamMaxThreadsPerItem);
  spdlog::debug(
      "[workshop] client.download start: {} items, {} parallel x {} threads",
      ids.size(), parallelItems, threadsPerItem);

  std::vector<std::uint64_t> itemIds;
  itemIds.reserve(ids.size());
  for (const auto& id : ids) {
    itemIds.push_back(*parseItemId(id));  // validated by download_mods
  }

  // PxSteamDL calls these on its worker threads. Their state lives on the main
  // thread, so each call is posted there.
  QObject* const target = &m_dispatch;
  pxsteamdl::Options options;
  options.parallel_items = static_cast<unsigned>(parallelItems);
  options.threads_per_item = static_cast<unsigned>(threadsPerItem);
  options.stop = stop;
  options.on_resolved = [this, target, batch](const pxsteamdl::ItemInfo& info) {
    QMetaObject::invokeMethod(
        target,
        [this, batch, id = itemIdText(info.item_id),
         title = QString::fromStdString(info.title)] {
          onResolved(*batch, id, title);
        },
        Qt::QueuedConnection);
  };
  options.on_progress = [this, target, batch](const pxsteamdl::Progress& p) {
    QMetaObject::invokeMethod(
        target,
        [this, batch, id = itemIdText(p.item_id),
         done = static_cast<qint64>(p.unpacked_bytes),
         total = static_cast<qint64>(p.unpacked_total)] {
          onProgress(*batch, id, done, total);
        },
        Qt::QueuedConnection);
  };

  downloadPhaseChanged.notify(QStringLiteral("query"));
  QFuture<Outcome> call = QtConcurrent::run(
      [client, itemIds = std::move(itemIds), rootPath, options]() -> Outcome {
        try {
          return client->download(itemIds, rootPath, options);
        } catch (const std::runtime_error& e) {
          // Anything else propagates to the caller, as in the Python version.
          const std::string what = e.what();
          return std::unexpected(what.empty() ? std::string("download failed")
                                              : what);
        }
      });
  Outcome outcome = co_await QFuture<Outcome>(call);

  if (!outcome) {
    // A failed call leaves the Steam session suspect; log in again next time.
    m_client.reset();
    const QString message = QString::fromStdString(outcome.error());
    for (const auto& id : ids) {
      finishItem(*batch, id, message);
    }
    co_return;
  }

  const auto& results = *outcome;
  // A network failure leaves the Steam session suspect; log in again next time.
  if (std::ranges::any_of(results, [](const pxsteamdl::Result& r) {
        return r.error_kind == pxsteamdl::ErrorKind::kNetwork;
      })) {
    m_client.reset();
  }

  // Progress posted by the worker is queued before the completion that resumes
  // this coroutine, so the item callbacks have already been handled here.
  QHash<QString, const pxsteamdl::Result*> byId;
  for (const auto& r : results) {
    byId.insert(itemIdText(r.item_id), &r);
  }
  for (const auto& id : ids) {
    const pxsteamdl::Result* result = byId.value(id, nullptr);
    if (!result) {
      finishItem(*batch, id, QStringLiteral("no result"));
    } else if (!result->cancelled()) {
      finishItem(*batch, id, QString::fromStdString(result->error));
    }
  }
}

DownloadResult SteamDownloader::finishBatch(
    const std::shared_ptr<SteamBatch>& batch) {
  m_stop.reset();
  m_activeIds.clear();
  busyChanged.notify(false);

  DownloadResult result{
      .succeeded = batch->succeeded,
      .failed = batch->failed,
      .changed = batch->changed(),
  };
  spdlog::info("[workshop] finished in {:.1f}s: {} ok, {} failed{}",
               std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                             batch->started)
                   .count(),
               result.succeeded.size(), result.failed.size(),
               result.failed.isEmpty() ? std::string()
                                       : " (failed ids: " +
                                             result.failed.mid(0, 20)
                                                 .join(QStringLiteral(", "))
                                                 .toStdString() +
                                             ")");
  recordSynced(result.succeeded);
  downloadFinished.notify(result);
  return result;
}

void SteamDownloader::finishItem(SteamBatch& batch, const QString& modId,
                                 const QString& error) {
  const qint64 total = batch.bytes.value(modId, {0, 0}).second;
  DownloadItemStatus status;
  if (!error.isEmpty()) {
    spdlog::warn("[workshop] {} failed: {}", modId.toStdString(),
                 error.toStdString());
    batch.failed.append(modId);
    status = DownloadItemStatus{
        .modId = modId,
        .status = QStringLiteral("error"),
        .bytesDone = 0,
        .bytesTotal = 0,
        .error = error,
    };
  } else {
    batch.succeeded.append(modId);
    batch.bytes.insert(modId, {total, total});
    status = DownloadItemStatus{
        .modId = modId,
        .status = QStringLiteral("success"),
        .bytesDone = total,
        .bytesTotal = total,
        .error = {},
    };
  }
  downloadItemStatusChanged.notify(status);
  downloadProgress.notify(batch.progress());
}

void SteamDownloader::recordSynced(const QStringList& ids) {
  if (ids.isEmpty()) {
    return;
  }
  const double now = unixNow();
  for (const auto& id : ids) {
    m_sync.synced[id.toStdString()] = now;
  }
  if (auto saved = m_config.save(kWorkshopSyncFile, m_sync); !saved) {
    spdlog::warn("[workshop] cannot save sync times: {}",
                 saved.error().message.toStdString());
  }
}

void SteamDownloader::onResolved(SteamBatch& batch, const QString& modId,
                                 const QString& title) {
  ++batch.resolved;
  if (!title.isEmpty()) {
    downloadItemTitled.notify(
        DownloadItemTitle{.modId = modId, .title = title});
  }
  if (batch.resolved >= batch.total) {
    downloadPhaseChanged.notify(QStringLiteral("run"));
  }
}

void SteamDownloader::onProgress(SteamBatch& batch, const QString& modId,
                                 qint64 done, qint64 total) {
  if (batch.succeeded.contains(modId) || batch.failed.contains(modId)) {
    return;
  }
  if (!batch.bytes.contains(modId)) {
    ++batch.seen;
    spdlog::debug("[workshop] {} first progress: {}/{} bytes (item {} of {})",
                  modId.toStdString(), done, total, batch.seen, batch.total);
  }
  batch.bytes.insert(modId, {done, total});

  // PxSteamDL reports per network chunk, thousands of times a second; the UI
  // needs far fewer.
  const auto now = std::chrono::steady_clock::now();
  if (done >= total || now - batch.lastEmitted >= kProgressEmitInterval) {
    batch.lastEmitted = now;
    emitItemProgress(batch, modId, done, total);
  }
  if (now - batch.lastLogged >= kProgressLogInterval) {
    batch.lastLogged = now;
    const DownloadProgress progress = batch.progress();
    spdlog::info("[workshop] progress: {}/{} items, {:.1f} MB, {:.0f}s elapsed",
                 progress.completed, progress.total,
                 static_cast<double>(progress.bytesDone) / 1048576.0,
                 std::chrono::duration<double>(now - batch.started).count());
  }
}

void SteamDownloader::emitItemProgress(SteamBatch& batch, const QString& modId,
                                       qint64 done, qint64 total) {
  downloadItemStatusChanged.notify(DownloadItemStatus{
      .modId = modId,
      .status = QStringLiteral("downloading"),
      .bytesDone = done,
      .bytesTotal = total,
      .error = {},
  });
  downloadProgress.notify(batch.progress());
}

QCoro::Task<void> SteamDownloader::autoUpdateOnce() {
  if (isDownloading()) {
    co_return;
  }
  const int hours = m_settings.value().auto_update_hours;
  if (hours <= 0) {
    co_return;
  }
  const QStringList ids = staleIds(hours * 3600.0, unixNow());
  if (ids.isEmpty()) {
    co_return;
  }
  spdlog::info("[workshop] auto-update: {} mods older than {}h", ids.size(),
               hours);
  const auto result = co_await downloadMods(ids);
  if (!result) {
    spdlog::warn("[workshop] auto-update not started: {}",
                 result.error().message.toStdString());
  }
}

void SteamDownloader::armAutoUpdate(std::chrono::milliseconds delay) {
  if (!m_stopping) {
    m_autoTimer.start(delay);
  }
}

QCoro::Task<void> SteamDownloader::runScheduledAutoUpdate() {
  try {
    co_await autoUpdateOnce();
  } catch (const std::exception& e) {
    spdlog::error("[workshop] auto-update iteration failed: {}", e.what());
  }
  armAutoUpdate(kAutoUpdateCheckInterval);
}

}  // namespace px::modrim

#endif  // PX_HAS_STEAM
