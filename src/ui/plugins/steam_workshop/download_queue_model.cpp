#include "ui/plugins/steam_workshop/download_queue_model.hpp"

namespace px::modrim {

DownloadQueueModel::DownloadQueueModel(QObject* parent)
    : QAbstractListModel(parent) {}

int DownloadQueueModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant DownloadQueueModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
    return {};
  const auto& item = m_items[index.row()];
  switch (role) {
    case kIdRole:
      return item.id;
    case kTitleRole:
      return item.title;
    case kDisplayRole:
      return item.display;
    case kStatusRole:
      return item.status;
    case kProgressRole:
      return item.progress;
    default:
      return {};
  }
}

QHash<int, QByteArray> DownloadQueueModel::roleNames() const {
  return {{kIdRole, "id"},
          {kTitleRole, "title"},
          {kDisplayRole, "display"},
          {kStatusRole, "status"},
          {kProgressRole, "progress"}};
}

void DownloadQueueModel::syncFrom(const CheckedDownloads& checked,
                                  const QHash<QString, QString>& statuses) {
  beginResetModel();
  m_items.clear();
  m_items.reserve(checked.size());
  for (const auto& [id, title] : checked) {
    m_items.append(
        Item{id, title.isEmpty() ? id : title,
             title.isEmpty() ? id : QStringLiteral("%1 (%2)").arg(title, id),
             statuses.value(id)});
  }
  endResetModel();
}

void DownloadQueueModel::updateStatus(const QString& id, const QString& status,
                                      qint64 done, qint64 total) {
  for (qsizetype pos = 0; pos < m_items.size(); ++pos) {
    auto& item = m_items[pos];
    if (item.id != id) continue;
    item.status = status;
    item.progress = total ? static_cast<double>(done) / total : 0;
    const auto idx = index(static_cast<int>(pos));
    emit dataChanged(idx, idx, {kStatusRole, kProgressRole});
    return;
  }
}

}  // namespace px::modrim
