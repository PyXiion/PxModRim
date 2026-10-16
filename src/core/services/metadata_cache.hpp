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
QString metadataCachePath(const QString& configDir);

// The symlink-resolved absolute form of `path`, as Python's os.path.realpath gives it. The path
// need not exist: components that do not exist are kept as written.
QString normalizePath(const QString& path);

// normalize_path for many paths; each shared parent directory is resolved once.
QStringList normalizePaths(const QStringList& paths);

// Persistent SQLite cache of parsed About.xml metadata (metadata-cache.db). All database work runs
// on one private worker thread, which owns the connection, so the connection is never used from two
// threads at once. A database that cannot be opened is recovered by deleting it; if that fails too,
// the cache disables itself and every lookup reports a miss.
class MetadataCache final : public providers::MetadataCacheStore {
public:
    explicit MetadataCache(QString path);
    ~MetadataCache() override;

    QCoro::Task<providers::CacheLookup> getMods(QList<providers::ModCandidate> candidates,
                                                 QString targetVersion) override;
    QCoro::Task<void> putMods(QList<providers::CachedMod> entries,
                               QString targetVersion) override;

    // Removes entries whose normalized mod path is not in `live_mod_paths`.
    QCoro::Task<void> pruneDisappeared(QSet<QString> liveModPaths);

    // Closes the connection; the next operation reopens it.
    QCoro::Task<void> close();

private:
    void closeConnection();
    QSqlDatabase* openConnection();
    Result<void> openDatabase();
    void removeDatabaseFiles() const;

    providers::CacheLookup lookup(const QList<providers::ModCandidate>& candidates,
                                  const QString& targetVersion);
    void store(const QList<providers::CachedMod>& entries, const QString& targetVersion);
    void prune(const QSet<QString>& liveModPaths);

    QString m_path;
    QString m_connectionName;
    QThreadPool m_worker;
    std::optional<QSqlDatabase> m_connection;  // worker thread only
    bool m_disabled = false;                   // worker thread only
};

}  // namespace px::modrim::services
