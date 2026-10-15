#pragma once

#include <QString>
#include <QStringList>

namespace px::core {

struct DownloadProgress {
    int total = 0;
    int completed = 0;
    qint64 bytes_done = 0;
    qint64 bytes_total = 0;
};

// `status` is "downloading", "success" or "error".
struct DownloadItemStatus {
    QString mod_id;
    QString status;
    qint64 bytes_done = 0;
    qint64 bytes_total = 0;
    QString error;
};

struct DownloadItemTitle {
    QString mod_id;
    QString title;
};

struct DownloadResult {
    QStringList succeeded;
    QStringList failed;
    // Succeeded items that actually transferred data (others were already current).
    QStringList changed;

    bool operator==(const DownloadResult&) const = default;
};

}  // namespace px::core
