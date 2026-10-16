#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include <QCoro/QCoroTask>
#include <QCoro/QCoroFuture>
#include <QHash>
#include <QList>
#include <QPromise>
#include <QRunnable>
#include <QString>
#include <QThreadPool>

#include "core/error.h"
#include "core/models/metadata/structures.h"
#include "core/profiler.h"

namespace px::modrim::providers {

// Runs `fn` on `pool`, or on the global pool when `pool` is null, and returns a future for its
// result. Awaiting the future from a coroutine resumes it on the calling thread.
template <class R, class F>
QFuture<R> runOnPool(QThreadPool* pool, F&& fn) {
    auto promise = std::make_shared<QPromise<R>>();
    QFuture<R> future = promise->future();
    QThreadPool* target = pool ? pool : QThreadPool::globalInstance();
    target->start(QRunnable::create([promise, fn = std::forward<F>(fn)]() mutable {
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

// A ModDirectory plus the About.xml stat data that cache entries are validated against.
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

// Persistent store of parsed About.xml metadata, keyed by About.xml path and validated by its
// mtime and size. Implemented by the cache service; providers only depend on this interface.
class MetadataCacheStore {
public:
    MetadataCacheStore() = default;
    MetadataCacheStore(const MetadataCacheStore&) = delete;
    MetadataCacheStore& operator=(const MetadataCacheStore&) = delete;
    virtual ~MetadataCacheStore() = default;

    // Returns the entries that are still valid for `target_version`; everything else is missing.
    virtual QCoro::Task<CacheLookup> get_mods(QList<ModCandidate> candidates,
                                              QString target_version) = 0;
    virtual QCoro::Task<void> put_mods(QList<CachedMod> entries, QString target_version) = 0;
};

// Discovered mods keyed by uuid (the mod path for mods on disk).
using ModMap = QHash<QString, ListedModPtr>;

// Lists the subdirectories of `modsPath` that contain an About/About.xml.
QList<ModDirectory> scanModDirectory(const QString& modsPath);

class BaseModProvider {
public:
    // `path` is the root this provider scans. `pool` and `metadata_cache` are optional and not
    // owned; they must outlive the provider.
    BaseModProvider(QString provider_id, QString color, QString path,
                    QThreadPool* pool = nullptr, MetadataCacheStore* metadata_cache = nullptr);
    BaseModProvider(const BaseModProvider&) = delete;
    BaseModProvider& operator=(const BaseModProvider&) = delete;
    virtual ~BaseModProvider() = default;

    const QString& provider_id() const noexcept { return provider_id_; }
    const QString& color() const noexcept { return color_; }
    const QString& path() const noexcept { return path_; }

    // `metadata_cache` overrides the cache given at construction for this call only.
    virtual QCoro::Task<Result<ModMap>> discover(QString target_version,
                                                 Profiler* timer = nullptr,
                                                 MetadataCacheStore* metadata_cache = nullptr,
                                                 bool force_reparse = false) = 0;

protected:
    // Loads metadata for `dirs`, using the cache where entries are still valid.
    QCoro::Task<Result<ModMap>> load_mods(QList<ModDirectory> dirs, QString target_version,
                                          Profiler& timer, MetadataCacheStore* metadata_cache,
                                          bool force_reparse);

private:
    QCoro::Task<Result<QList<ParsedMod>>> parse_candidates(QList<ModCandidate> missing,
                                                           QString target_version);

    QString provider_id_;
    QString color_;
    QString path_;
    QThreadPool* pool_;
    MetadataCacheStore* metadata_cache_;
};

}  // namespace px::modrim::providers
