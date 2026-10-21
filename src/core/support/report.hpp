#pragma once

#include <QHash>
#include <QLatin1StringView>
#include <QString>
#include <QStringList>
#include <chrono>
#include <optional>

#include <QCoro/QCoroTask>

#include "core/config.hpp"
#include "core/error.hpp"
#include "core/models/metadata/structures.hpp"

class QNetworkAccessManager;

namespace px::modrim::support {

inline constexpr qint64 kMaxLogBytes = 2 * 1024 * 1024;
inline constexpr QLatin1StringView kDefaultUploadEndpoint{"https://paste.rs/"};

// The app state a diagnostic report describes. Copied out of the live
// application so that building a report never touches it.
struct ReportSource {
  QString gameVersion = QStringLiteral("Unknown");
  PathConfig paths;
  // Keyed by uuid, as the mod registry is.
  QHash<QString, ListedModPtr> allMods;
  QStringList activeUuids;
};

// Replaces the home directory with "~" and user-directory usernames with
// "<user>". The home directory and username default to the current user's when
// not given.
QString redact(const QString& text,
               const std::optional<QString>& homeDir = std::nullopt,
               const std::optional<QString>& username = std::nullopt);

// The last `max_bytes` of the log as plain text, with loguru JSON records
// unpacked. Defaults to the app's own log under the config directory.
QString readLogTail(const std::optional<QString>& logPath = std::nullopt,
                    qint64 maxBytes = kMaxLogBytes);

// Plain-text system and log report, with the user's details redacted.
QString buildReport(const ReportSource& source,
                    const std::optional<QString>& logPath = std::nullopt,
                    const std::optional<QString>& homeDir = std::nullopt,
                    const std::optional<QString>& username = std::nullopt,
                    qint64 maxLogBytes = kMaxLogBytes);

// Posts plain-text reports to a paste service. Not owned; `network` must
// outlive the uploader.
class PasteUploader {
 public:
  PasteUploader(QNetworkAccessManager& network,
                QString endpoint = QString(kDefaultUploadEndpoint),
                std::chrono::milliseconds timeout = std::chrono::seconds(30));

  // Resolves to the URL the service returned, or to a user-facing error
  // message.
  QCoro::Task<Result<QString>> upload(QString text);

 private:
  QNetworkAccessManager& m_network;
  QString m_endpoint;
  std::chrono::milliseconds m_timeout;
};

}  // namespace px::modrim::support
