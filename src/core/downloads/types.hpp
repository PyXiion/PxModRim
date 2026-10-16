#pragma once

#include <QString>
#include <QStringList>

namespace px::modrim {

struct DownloadProgress {
    int total = 0;
    int completed = 0;
    qint64 bytesDone = 0;
    qint64 bytesTotal = 0;
};

// `status` is "downloading", "success" or "error".
struct DownloadItemStatus {
    QString modId;
    QString status;
    qint64 bytesDone = 0;
    qint64 bytesTotal = 0;
    QString error;
};

struct DownloadItemTitle {
    QString modId;
    QString title;
};

struct DownloadResult {
    QStringList succeeded;
    QStringList failed;
    // Succeeded items that actually transferred data (others were already current).
    QStringList changed;

    bool operator==(const DownloadResult&) const = default;
};

}  // namespace px::modrim
