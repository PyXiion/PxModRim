#pragma once

#include <QLatin1StringView>
#include <QString>
#include <optional>

#include <QCoro/QCoroTask>

#include "core/error.hpp"

class QNetworkAccessManager;

namespace px::modrim::services {

inline constexpr QLatin1StringView kLatestReleaseUrl{
    "https://api.github.com/repos/PyXiion/PxModRim/releases/latest"};

struct ReleaseInfo {
  QString tag;
  QString name;
  QString notes;
  QString url;
};

// True when `tag` is a strictly higher release than `current`. A pre-release
// suffix ("-rc1") sorts below the plain release with the same numbers. Versions
// that do not parse are never reported as outdated.
bool isNewer(const QString& tag, const QString& current);

// Looks up the newest published GitHub release.
class UpdateService {
 public:
  // `network` is not owned and must outlive the service. `releaseUrl` is the
  // latest-release endpoint; the default is the project's own.
  UpdateService(QString currentVersion, QNetworkAccessManager& network,
                QString releaseUrl = QString(kLatestReleaseUrl));

  const QString& currentVersion() const noexcept { return m_current; }

  // The latest release when it is newer than the running app, nullopt
  // otherwise. Fails on a network error, a non-success HTTP status or a
  // malformed response.
  QCoro::Task<Result<std::optional<ReleaseInfo>>> check();

 private:
  QString m_current;
  QNetworkAccessManager& m_network;
  QString m_releaseUrl;
};

}  // namespace px::modrim::services
