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
  enum Role {
    kIdRole = Qt::UserRole + 1,
    kDisplayRole,
    kStatusRole,
    kProgressRole
  };
  explicit DownloadQueueModel(QObject* parent = nullptr);
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;
  void syncFrom(const CheckedDownloads& checked,
                const QHash<QString, QString>& statuses);
  void updateStatus(const QString& id, const QString& status, qint64 done,
                    qint64 total);

 private:
  struct Item {
    QString id;
    QString display;
    QString status;
    double progress = 0;
  };
  QList<Item> m_items;
};

}  // namespace px::modrim
