#pragma once

#include <optional>
#include <vector>

#include <QCoro/QCoroTask>
#include <QLatin1StringView>
#include <QString>

#include "core/error.h"

class QNetworkAccessManager;

namespace px::modrim::services {

inline constexpr QLatin1StringView LATEST_RELEASE_URL{
    "https://api.github.com/repos/PyXiion/PxModRim/releases/latest"};

struct ReleaseInfo {
    QString tag;
    QString name;
    QString notes;
    QString url;
};

// The numeric components of `text` ("v1.2.3" -> 1, 2, 3), ignoring any suffix; nullopt when the
// text does not start with a version.
std::optional<std::vector<qint64>> parse_version(const QString& text);

// True when `tag` is a strictly higher release than `current`. A pre-release suffix ("-rc1")
// sorts below the plain release with the same numbers. Versions that do not parse are never
// reported as outdated.
bool is_newer(const QString& tag, const QString& current);

// Looks up the newest published GitHub release.
class UpdateService {
public:
    // `network` is not owned and must outlive the service. `release_url` is the latest-release
    // endpoint; the default is the project's own.
    UpdateService(QString current_version, QNetworkAccessManager& network,
                  QString release_url = QString(LATEST_RELEASE_URL));

    const QString& current_version() const noexcept { return current_; }

    // The latest release when it is newer than the running app, nullopt otherwise. Fails on a
    // network error, a non-success HTTP status or a malformed response.
    QCoro::Task<Result<std::optional<ReleaseInfo>>> check();

private:
    QString current_;
    QNetworkAccessManager& network_;
    QString release_url_;
};

}  // namespace px::modrim::services
