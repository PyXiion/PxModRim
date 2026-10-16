#include "ui/plugins/steam_workshop/download_queue_model.hpp"

namespace px::modrim {

DownloadQueueModel::DownloadQueueModel(QObject* parent) : QAbstractListModel(parent) {}

int DownloadQueueModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

QVariant DownloadQueueModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= items_.size()) return {};
    const auto& item = items_[index.row()];
    switch (role) {
    case IdRole: return item.id;
    case TitleRole: return item.title;
    case DisplayRole: return item.display;
    case StatusRole: return item.status;
    case ProgressRole: return item.progress;
    default: return {};
    }
}

QHash<int, QByteArray> DownloadQueueModel::roleNames() const {
    return {{IdRole, "id"}, {TitleRole, "title"}, {DisplayRole, "display"}, {StatusRole, "status"}, {ProgressRole, "progress"}};
}

void DownloadQueueModel::sync_from(const CheckedDownloads& checked, const QHash<QString, QString>& statuses) {
    beginResetModel();
    items_.clear();
    items_.reserve(checked.size());
    for (const auto& [id, title] : checked) {
        items_.append(Item{id, title.isEmpty() ? id : title,
                           title.isEmpty() ? id : QStringLiteral("%1 (%2)").arg(title, id), statuses.value(id)});
    }
    endResetModel();
}

void DownloadQueueModel::update_status(const QString& id, const QString& status, qint64 done, qint64 total) {
    for (qsizetype pos = 0; pos < items_.size(); ++pos) {
        auto& item = items_[pos];
        if (item.id != id) continue;
        item.status = status;
        item.progress = total ? static_cast<double>(done) / total : 0;
        const auto idx = index(static_cast<int>(pos));
        emit dataChanged(idx, idx, {StatusRole, ProgressRole});
        return;
    }
}

}  // namespace px::modrim
