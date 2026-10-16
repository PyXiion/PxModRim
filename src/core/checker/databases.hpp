#pragma once

#include <optional>

#include <QCoro/QCoroTask>
#include <QHash>
#include <QSet>
#include <QString>

#include "core/checker/models.hpp"
#include "core/models/metadata/structures.hpp"

class QNetworkAccessManager;

namespace px::modrim {

class ConfigService;

namespace checker {

// Identity of a cache file: changes whenever the file is rewritten.
struct FileStamp {
    qint64 modifiedMs = 0;
    qint64 size = 0;

    bool operator==(const FileStamp&) const = default;
};

// Caches the NoVersionWarning package-id list under the config directory and refreshes it from
// GitHub when the cache is missing or a refresh is forced.
class NoVersionWarningService {
public:
    NoVersionWarningService(const ConfigService& configService, QNetworkAccessManager& network);

    // Cached ids, reloaded when the file changed on disk. Downloads when the cache is missing
    // or `force` is set. A failed download yields an empty set and keeps the previous ids.
    QCoro::Task<QSet<PackageId>> ensure(bool force = false);

    // Seconds since the cache file was written, or nullopt when there is no cache.
    std::optional<double> cacheAgeS() const;

    // Loads the cache synchronously when it exists; otherwise returns the current ids.
    QSet<PackageId> loadIfExists();

private:
    QCoro::Task<QSet<PackageId>> download();

    QNetworkAccessManager& m_network;
    QString m_cacheDir;
    QString m_xmlPath;
    QSet<PackageId> m_pids;
    // Stamp of the file the current `pids_` were parsed from.
    std::optional<FileStamp> m_loadedStamp;
};

// Caches the UseThisInstead replacement database (gzipped JSON) under the config directory and
// refreshes it from GitHub when the cache is missing or a refresh is forced.
class UseThisInsteadService {
public:
    UseThisInsteadService(const ConfigService& configService, QNetworkAccessManager& network);

    // Cached entries, reloaded when the file changed on disk. Downloads when the cache is
    // missing or `force` is set. A failed download yields an empty map and keeps the previous
    // entries.
    QCoro::Task<QHash<QString, ReplacementInfo>> ensure(bool force = false);

    std::optional<double> cacheAgeS() const;

    QHash<QString, ReplacementInfo> loadIfExists();

private:
    QCoro::Task<QHash<QString, ReplacementInfo>> download();

    QNetworkAccessManager& m_network;
    QString m_cacheDir;
    QString m_jsonPath;
    QHash<QString, ReplacementInfo> m_entries;
    std::optional<FileStamp> m_loadedStamp;
};

}  // namespace checker
}  // namespace px::modrim
