#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <optional>

#include <QCoro/QCoroTask>

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

// Caches a database file under the config directory and refreshes it from
// GitHub when the cache is missing or a refresh is forced. `Entries` selects
// the database: QSet<PackageId> for NoVersionWarning, the replacement map for
// UseThisInstead.
template <typename Entries>
class CachedDatabase {
 public:
  CachedDatabase(const ConfigService& configService,
                 QNetworkAccessManager& network);

  // Cached entries, reloaded when the file changed on disk. Downloads when the
  // cache is missing or `force` is set. A failed download yields empty entries
  // and keeps the previous ones.
  QCoro::Task<Entries> ensure(bool force = false);

  // Seconds since the cache file was written, or nullopt when there is no
  // cache.
  std::optional<double> cacheAgeS() const;

  // Loads the cache synchronously when it exists; otherwise returns the current
  // entries.
  Entries loadIfExists();

 private:
  struct Source {
    QString label;
    QString url;
    QString fileName;
    Entries (*parse)(const QString& path);
  };
  static const Source& source();

  QCoro::Task<Entries> download();

  QNetworkAccessManager& m_network;
  QString m_cacheDir;
  QString m_path;
  Entries m_entries;
  // Stamp of the file the current `m_entries` were parsed from.
  std::optional<FileStamp> m_loadedStamp;
};

using NoVersionWarningDb = CachedDatabase<QSet<PackageId>>;
using UseThisInsteadDb = CachedDatabase<QHash<QString, ReplacementInfo>>;

}  // namespace checker
}  // namespace px::modrim
