#pragma once

#include <chrono>
#include <optional>

#include <QCoro/QCoroTask>
#include <QHash>
#include <QLatin1StringView>
#include <QString>
#include <QStringList>

#include "core/config.h"
#include "core/error.h"
#include "core/models/metadata/structures.h"

class QNetworkAccessManager;

namespace px::core::support {

inline constexpr qint64 MAX_LOG_BYTES = 2 * 1024 * 1024;
inline constexpr QLatin1StringView DEFAULT_UPLOAD_ENDPOINT{"https://paste.rs/"};

// The app state a diagnostic report describes. Copied out of the live application so that
// building a report never touches it.
struct ReportSource {
    QString game_version = QStringLiteral("Unknown");
    PathConfig paths;
    // Keyed by uuid, as the mod registry is.
    QHash<QString, ListedModPtr> all_mods;
    QStringList active_uuids;
};

// Replaces the home directory with "~" and user-directory usernames with "<user>". The home
// directory and username default to the current user's when not given.
QString redact(const QString& text, const std::optional<QString>& home_dir = std::nullopt,
               const std::optional<QString>& username = std::nullopt);

// The last `max_bytes` of the log as plain text, with loguru JSON records unpacked. Defaults to
// the app's own log under the config directory.
QString read_log_tail(const std::optional<QString>& log_path = std::nullopt,
                      qint64 max_bytes = MAX_LOG_BYTES);

// Plain-text system and log report, with the user's details redacted.
QString build_report(const ReportSource& source,
                     const std::optional<QString>& log_path = std::nullopt,
                     const std::optional<QString>& home_dir = std::nullopt,
                     const std::optional<QString>& username = std::nullopt,
                     qint64 max_log_bytes = MAX_LOG_BYTES);

// Posts plain-text reports to a paste service. Not owned; `network` must outlive the uploader.
class PasteUploader {
public:
    PasteUploader(QNetworkAccessManager& network,
                  QString endpoint = QString(DEFAULT_UPLOAD_ENDPOINT),
                  std::chrono::milliseconds timeout = std::chrono::seconds(30));

    // Resolves to the URL the service returned, or to a user-facing error message.
    QCoro::Task<Result<QString>> upload(QString text);

private:
    QNetworkAccessManager& network_;
    QString endpoint_;
    std::chrono::milliseconds timeout_;
};

}  // namespace px::core::support
