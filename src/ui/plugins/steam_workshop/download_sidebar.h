#pragma once

#include <QWidget>

#include "ui/plugins/steam_workshop/download_queue_model.h"

class QQmlEngine;
class QQuickWidget;

namespace px::modrim {

class DownloadSidebar final : public QWidget {
    Q_OBJECT
public:
    DownloadSidebar(QQmlEngine* engine, QWidget* parent);
    Q_INVOKABLE void downloadRequested();
    Q_INVOKABLE void stopRequested();
    Q_INVOKABLE void removeItem(const QString& id);
    Q_INVOKABLE void clearQueue();
    void set_download_enabled(bool enabled);
    void sync_from(const CheckedDownloads& checked, const QHash<QString, QString>& statuses = {});
    void update_status(const QString& id, const QString& status, qint64 done = 0, qint64 total = 0);
signals:
    void download_requested();
    void stop_requested();
    void item_removed(const QString& id);
    void clear_requested();
private:
    DownloadQueueModel* model_;
    QQuickWidget* qml_;
};

}  // namespace px::modrim
