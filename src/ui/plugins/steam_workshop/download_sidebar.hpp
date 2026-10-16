#pragma once

#include <QWidget>

#include "ui/plugins/steam_workshop/download_queue_model.hpp"

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
    void setDownloadEnabled(bool enabled);
    void syncFrom(const CheckedDownloads& checked, const QHash<QString, QString>& statuses = {});
    void updateStatus(const QString& id, const QString& status, qint64 done = 0, qint64 total = 0);
signals:
    void downloadTriggered();
    void stopTriggered();
    void itemRemoved(const QString& id);
    void clearRequested();
private:
    DownloadQueueModel* m_model;
    QQuickWidget* m_qml;
};

}  // namespace px::modrim
