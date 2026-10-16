#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

namespace px::modrim {

using CheckedDownloads = QList<QPair<QString, QString>>;

class DownloadQueueModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { IdRole = Qt::UserRole + 1, TitleRole, DisplayRole, StatusRole, ProgressRole };
    explicit DownloadQueueModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void sync_from(const CheckedDownloads& checked, const QHash<QString, QString>& statuses = {});
    void update_status(const QString& id, const QString& status, qint64 done = 0, qint64 total = 0);

private:
    struct Item {
        QString id;
        QString title;
        QString display;
        QString status;
        double progress = 0;
    };
    QList<Item> items_;
};

}  // namespace px::modrim
