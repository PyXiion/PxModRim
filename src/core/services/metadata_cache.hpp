#pragma once

#include <optional>

#include <QCoro/QCoroTask>
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include "core/providers/base.hpp"

namespace px::modrim::services {

// The cache database inside the config directory.
QString metadata_cache_path(const QString& config_dir);

// The symlink-resolved absolute form of `path`, as Python's os.path.realpath gives it. The path
// need not exist: components that do not exist are kept as written.
QString normalize_path(const QString& path);

// normalize_path for many paths; each shared parent directory is resolved once.
QStringList normalize_paths(const QStringList& paths);

// Persistent SQLite cache of parsed About.xml metadata (metadata-cache.db). All database work runs
// on one private worker thread, which owns the connection, so the connection is never used from two
// threads at once. A database that cannot be opened is recovered by deleting it; if that fails too,
// the cache disables itself and every lookup reports a miss.
class MetadataCache final : public providers::MetadataCacheStore {
public:
    explicit MetadataCache(QString path);
    ~MetadataCache() override;

    QCoro::Task<providers::CacheLookup> get_mods(QList<providers::ModCandidate> candidates,
                                                 QString target_version) override;
    QCoro::Task<void> put_mods(QList<providers::CachedMod> entries,
                               QString target_version) override;

    // Removes entries whose normalized mod path is not in `live_mod_paths`.
    QCoro::Task<void> prune_disappeared(QSet<QString> live_mod_paths);

    // Closes the connection; the next operation reopens it.
    QCoro::Task<void> close();

private:
    void close_connection();
    QSqlDatabase* open_connection();
    Result<void> open_database();
    void remove_database_files() const;

    providers::CacheLookup lookup(const QList<providers::ModCandidate>& candidates,
                                  const QString& target_version);
    void store(const QList<providers::CachedMod>& entries, const QString& target_version);
    void prune(const QSet<QString>& live_mod_paths);

    QString path_;
    QString connection_name_;
    QThreadPool worker_;
    std::optional<QSqlDatabase> connection_;  // worker thread only
    bool disabled_ = false;                   // worker thread only
};

}  // namespace px::modrim::services
