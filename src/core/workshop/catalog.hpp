#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <any>
#include <coroutine>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <QCoro/QCoroFuture>
#include <QCoro/QCoroTask>
#include <QtConcurrent/QtConcurrentRun>

#include <glaze/glaze.hpp>
#include <spdlog/spdlog.h>

#include "core/config.hpp"
#include "core/downloads/manager.hpp"
#include "core/error.hpp"
#include "core/events.hpp"
#include "core/file_io.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/plugin.hpp"
#include "core/plugin_config.hpp"
#include "core/workshop/client.hpp"
#include "core/workshop/favourites.hpp"
#include "core/workshop/types.hpp"

namespace px::modrim::workshop {

inline constexpr std::string_view kDefaultCatalogUrl =
    "https://api.modrim.pyxiion.dev";

// A value remembered for less than this many seconds is served without
// contacting the catalog.
inline constexpr double kFreshSeconds = 300.0;

// Stored as plugins/workshop_catalog.json.
// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
struct CatalogSettings {
  std::string url = std::string(kDefaultCatalogUrl);

  bool operator==(const CatalogSettings&) const = default;
};
// NOLINTEND(readability-identifier-naming)

// The installed mods the catalog compares against. The composition root
// supplies it, so the catalog does not reach for global state.
using InstalledModsProvider = std::function<QList<ListedModPtr>()>;

// A cached value with the time it was fetched (seconds since the epoch).
// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on disk or
// on the wire
template <class T>
struct Remembered {
  double at = 0;
  T value{};
};
// NOLINTEND(readability-identifier-naming)

namespace detail {

// A missing, unreadable or undecodable cache file is treated as a miss.
template <class T>
std::optional<Remembered<T>> readCached(const QString& path) {
  auto bytes = readFileBytes(path);
  if (!bytes) {
    return std::nullopt;
  }
  Remembered<T> entry;
  if (glz::read<kWireOpts>(entry, *bytes)) {
    return std::nullopt;
  }
  return entry;
}

// Writes one cache file, then drops the oldest files beyond the disk limit.
Result<void> writeCached(const QString& directory, const QString& path,
                         const std::string& text);

}  // namespace detail

// The Workshop catalog: remote browsing and resolution of mods, plus a download
// queue that runs behind the downloads manager. The catalog never mutates
// installed mods itself.
class WorkshopCatalog : public Plugin {
 public:
  WorkshopCatalog();
  void setup(CoreContext& ctx) override;
  // `config` may be null: settings, favourites and the response cache then stay
  // in memory.
  WorkshopCatalog(ConfigService* config, DownloadManager& downloads,
                  InstalledModsProvider installedMods);
  ~WorkshopCatalog();

  WorkshopCatalog(const WorkshopCatalog&) = delete;
  WorkshopCatalog& operator=(const WorkshopCatalog&) = delete;

  // Stops the queue, aborts requests, and waits for the queue runner to finish.
  QCoro::Task<void> shutdown() override;

  Event<QString> catalogUrlChanged;
  Event<std::monostate> installedChanged;
  Event<std::monostate> queueChanged;

  const CatalogSettings& settings() const noexcept {
    return m_settings.value();
  }
  void setBaseUrl(const QString& url);
  bool configured() const noexcept { return !m_baseUrl.isEmpty(); }

  FavouriteCollections& favourites() noexcept { return m_favourites; }

  QCoro::Task<CatalogResult<Discover>> discover();
  QCoro::Task<CatalogResult<CatalogPage<CatalogMod>>> mods(CatalogQuery query);
  QCoro::Task<CatalogResult<CatalogPage<CatalogCollection>>> collections(
      CatalogQuery query);
  QCoro::Task<CatalogResult<std::optional<CatalogMod>>> mod(QString id);
  QCoro::Task<CatalogResult<std::optional<CollectionDetail>>> collection(
      QString id);

  // Serves the remembered value at once through `show`, then the refreshed one
  // when it differs. A value younger than `fresh_for` is served without
  // contacting the catalog. With `persist`, the value also survives a restart.
  // `T` must be serialisable with glaze and comparable.
  template <class T>
  QCoro::Task<CatalogResult<void>> cached(
      QString key, std::function<QCoro::Task<CatalogResult<T>>()> produce,
      std::function<void(const T&)> show, double freshFor = kFreshSeconds,
      bool persist = false);

  // Installed state is cached until invalidate_installed().
  InstallState installState(const CatalogMod& mod);
  QCoro::Task<CatalogResult<std::vector<CatalogMod>>> installedWithUpdates();

  // Resolves ids into the mods to fetch, skipping those already current.
  QCoro::Task<CatalogResult<DownloadPlan>> plan(QStringList modIds,
                                                QStringList collectionIds);
  QCoro::Task<Result<DownloadResult>> download(DownloadPlan downloadPlan);

  // Ids waiting in the queue or downloading.
  QSet<QString> queuedIds() const { return m_queued; }
  // Queues `plan` behind any running download without waiting for it.
  void enqueue(const DownloadPlan& downloadPlan);
  void cancelQueue();

  // Call when the installed mods changed (mods changed, config changed).
  void invalidateInstalled();

 private:
  using InstalledIndex = QHash<QString, QList<ListedModPtr>>;

  // Suspends the queue until the downloads report they are idle again.
  struct IdleWait {
    WorkshopCatalog& catalog;

    // NOLINTBEGIN(readability-identifier-naming): coroutine awaiter protocol
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) const {
      catalog.m_idleWaiters.push_back(handle);
    }
    void await_resume() const noexcept {}
    // NOLINTEND(readability-identifier-naming)
  };

  CatalogClient& client();
  const InstalledIndex& installed();
  void bindDownloads(DownloadManager& downloads);
  void disconnectEvents();
  InstallState installStateOf(const CatalogMod& mod,
                              const InstalledIndex& installed) const;
  void changeUrl(const QString& url);
  void resumeIdleWaiters();
  QCoro::Task<void> drain();

  template <class T>
  std::optional<Remembered<T>> recall(const QString& key) const;
  // Keeps the memory cache within its limit. `move_to_end` marks the entry as
  // the newest.
  void remember(const QString& key, std::any value, bool moveToEnd);
  QString cacheFile(const QString& key) const;

  PluginConfig<CatalogSettings> m_settings;
  QString m_baseUrl;
  std::optional<QString> m_cacheDir;
  FavouriteCollections m_favourites;
  DownloadManager* m_downloads = nullptr;
  InstalledModsProvider m_installedMods;
  Event<DownloadResult>::Id m_downloadFinishedId = 0;
  Event<bool>::Id m_busyChangedId = 0;
  CoreContext* m_context = nullptr;
  Event<std::monostate>::Id m_modsChangedId = 0;
  Event<AppConfig>::Id m_configChangedId = 0;

  std::optional<InstalledIndex> m_installedIndex;
  // Insertion-ordered: the oldest entries are evicted first.
  QList<QString> m_responseOrder;
  // Each value is a Remembered<T> for the T it was stored with.
  QHash<QString, std::any> m_responses;

  // Every client made so far stays alive until shutdown, so requests in flight
  // on a replaced client finish.
  std::vector<std::unique_ptr<CatalogClient>> m_clients;
  CatalogClient* m_client = nullptr;

  QList<QStringList> m_queue;
  QSet<QString> m_queued;
  // Resetting a QCoro task does not cancel its suspended coroutine.
  std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
  bool m_stopping = false;
  bool m_downloading = false;
  bool m_draining = false;
  std::optional<QCoro::Task<void>> m_runner;
  std::vector<std::coroutine_handle<>> m_idleWaiters;
};

template <class T>
std::optional<Remembered<T>> WorkshopCatalog::recall(const QString& key) const {
  const auto found = m_responses.constFind(key);
  if (found == m_responses.constEnd()) {
    return std::nullopt;
  }
  const auto* typed = std::any_cast<Remembered<T>>(&found.value());
  if (!typed) {
    return std::nullopt;
  }
  return *typed;
}

template <class T>
QCoro::Task<CatalogResult<void>> WorkshopCatalog::cached(
    QString key, std::function<QCoro::Task<CatalogResult<T>>()> produce,
    std::function<void(const T&)> show, double freshFor, bool persist) {
  const QString fullKey = m_baseUrl + QLatin1Char('|') + key;
  std::optional<QString> path;
  if (persist && m_cacheDir) {
    path = cacheFile(fullKey);
  }

  std::optional<Remembered<T>> entry = recall<T>(fullKey);
  if (!entry && path) {
    entry = co_await QtConcurrent::run(
        [path = *path] { return detail::readCached<T>(path); });
    if (entry) {
      remember(fullKey, *entry, false);
    }
  }
  if (entry) {
    show(entry->value);
    const double age = QDateTime::currentMSecsSinceEpoch() / 1000.0 - entry->at;
    if (age < freshFor) {
      co_return {};
    }
  }

  auto fetched = co_await produce();
  if (!fetched) {
    co_return std::unexpected(fetched.error());
  }
  T value = std::move(*fetched);
  const bool changed = !entry || value != entry->value;
  if (entry && !changed) {
    // Keep the displayed copy so an equal refresh does not replace what is on
    // screen.
    value = entry->value;
  }
  const Remembered<T> remembered{QDateTime::currentMSecsSinceEpoch() / 1000.0,
                                 value};
  remember(fullKey, remembered, true);

  if (path) {
    auto encoded = glz::write<kWireOpts>(remembered);
    if (encoded) {
      auto saved = co_await QtConcurrent::run(
          [directory = *m_cacheDir, file = *path, bytes = std::move(*encoded)] {
            return detail::writeCached(directory, file, bytes);
          });
      if (!saved) {
        spdlog::warn("[workshop] cannot write cache: {}",
                     saved.error().message.toStdString());
      }
    }
  }
  if (changed) {
    show(value);
  }
  co_return {};
}

}  // namespace px::modrim::workshop
