#pragma once

#include <QHash>
#include <QList>
#include <QPromise>
#include <QRunnable>
#include <QString>
#include <QThreadPool>
#include <memory>
#include <type_traits>
#include <utility>

#include <QCoro/QCoroFuture>
#include <QCoro/QCoroTask>

#include "core/error.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/profiler.hpp"

namespace px::modrim::providers {

// Runs `fn` on `pool`, or on the global pool when `pool` is null, and returns a
// future for its result. Awaiting the future from a coroutine resumes it on the
// calling thread.
template <class R, class F>
QFuture<R> runOnPool(QThreadPool* pool, F&& fn) {
  auto promise = std::make_shared<QPromise<R>>();
  QFuture<R> future = promise->future();
  QThreadPool* target = pool ? pool : QThreadPool::globalInstance();
  target->start(
      QRunnable::create([promise, fn = std::forward<F>(fn)]() mutable {
        promise->start();
        if constexpr (std::is_void_v<R>) {
          fn();
        } else {
          promise->addResult(fn());
        }
        promise->finish();
      }));
  return future;
}

// A directory that holds a mod, with the About.xml that describes it.
struct ModDirectory {
  QString modPath;
  QString aboutXmlPath;
};

// A ModDirectory plus the About.xml stat data that cache entries are validated
// against.
struct ModCandidate {
  QString modPath;
  QString aboutXmlPath;
  double mtime = 0.0;
  qint64 size = 0;
};

// A freshly parsed mod and the candidate it came from.
struct ParsedMod {
  ListedModPtr mod;
  ModCandidate candidate;
};

// An AboutXmlMod ready to be stored by a metadata cache.
struct CachedMod {
  std::shared_ptr<AboutXmlMod> mod;
  ModCandidate candidate;
};

struct CacheLookup {
  QList<ListedModPtr> cached;
  QList<ModCandidate> missing;
};

// Persistent store of parsed About.xml metadata, keyed by About.xml path and
// validated by its mtime and size. Implemented by the cache service; providers
// only depend on this interface.
class MetadataCacheStore {
 public:
  MetadataCacheStore() = default;
  MetadataCacheStore(const MetadataCacheStore&) = delete;
  MetadataCacheStore& operator=(const MetadataCacheStore&) = delete;
  virtual ~MetadataCacheStore() = default;

  // Returns the entries that are still valid for `target_version`; everything
  // else is missing.
  virtual QCoro::Task<CacheLookup> getMods(QList<ModCandidate> candidates,
                                           QString targetVersion) = 0;
  virtual QCoro::Task<void> putMods(QList<CachedMod> entries,
                                    QString targetVersion) = 0;
};

// Discovered mods keyed by uuid (the mod path for mods on disk).
using ModMap = QHash<QString, ListedModPtr>;

// Lists the subdirectories of `modsPath` that contain an About/About.xml.
QList<ModDirectory> scanModDirectory(const QString& modsPath);

class BaseModProvider {
 public:
  // `path` is the root this provider scans. `pool` is optional and not owned;
  // it must outlive the provider.
  BaseModProvider(QString providerId, QString color, QString path,
                  QThreadPool* pool = nullptr);
  BaseModProvider(const BaseModProvider&) = delete;
  BaseModProvider& operator=(const BaseModProvider&) = delete;
  virtual ~BaseModProvider() = default;

  const QString& providerId() const noexcept { return m_providerId; }
  const QString& color() const noexcept { return m_color; }
  const QString& path() const noexcept { return m_path; }

  // `metadataCache` serves this call only; null parses everything.
  QCoro::Task<Result<ModMap>> discover(
      QString targetVersion, Profiler* timer = nullptr,
      MetadataCacheStore* metadataCache = nullptr, bool forceReparse = false);

 protected:
  // The mod directories this provider offers. Blocking: runs on the thread
  // pool.
  virtual QList<ModDirectory> scanDirectories() const = 0;

  // Loads metadata for `dirs`, using the cache where entries are still valid.
  QCoro::Task<Result<ModMap>> loadMods(QList<ModDirectory> dirs,
                                       QString targetVersion, Profiler& timer,
                                       MetadataCacheStore* metadataCache,
                                       bool forceReparse);

 private:
  QCoro::Task<Result<QList<ParsedMod>>> parseCandidates(
      QList<ModCandidate> missing, QString targetVersion);

  QString m_providerId;
  QString m_color;
  QString m_path;
  QThreadPool* m_pool;
};

}  // namespace px::modrim::providers
