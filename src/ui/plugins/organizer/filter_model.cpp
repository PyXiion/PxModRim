#include "ui/plugins/organizer/filter_model.h"

namespace px::modrim {

OrganizerFilterModel::OrganizerFilterModel(QObject* parent) : QAbstractListModel(parent) {}

int OrganizerFilterModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

QVariant OrganizerFilterModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= items_.size()) return {};
    const auto& entry = items_[index.row()];
    const bool tag = entry.tag_id.has_value() || entry.key.startsWith("tag:");
    switch (role) {
        case Qt::DisplayRole:
        case LabelRole: return entry.label;
        case CountRole: return entry.count;
        case KeyRole: return entry.key;
        case SectionRole:
            if (tag) return QStringLiteral("Tags");
            if (entry.provider_id || entry.key.startsWith("provider:")) return QStringLiteral("Providers");
            return QStringLiteral("Status");
        case IconRole:
            if (tag) return QStringLiteral("tag");
            if (entry.key == "all") return QStringLiteral("grid");
            if (entry.key == "active") return QStringLiteral("check-circle");
            if (entry.key == "inactive") return QStringLiteral("ban");
            if (entry.provider_id == "steam" || entry.provider_id == "downloaded") return QStringLiteral("steam");
            if (entry.provider_id == "core") return QStringLiteral("grid");
            if (entry.provider_id == "git") return QStringLiteral("git");
            return QStringLiteral("folder");
        default: return {};
    }
}

QHash<int, QByteArray> OrganizerFilterModel::roleNames() const {
    return {{Qt::DisplayRole, "filterLabel"}, {CountRole, "count"},
        {IconRole, "iconName"}, {SectionRole, "sectionName"}, {KeyRole, "key"}};
}

void OrganizerFilterModel::update(const QList<organizer::TreeFilter>& entries) {
    bool same_keys = entries.size() == items_.size();
    for (qsizetype i = 0; same_keys && i < entries.size(); ++i)
        same_keys = entries[i].key == items_[i].key;
    if (!same_keys) {
        beginResetModel();
        items_ = entries;
        endResetModel();
    } else {
        items_ = entries;
        if (!items_.isEmpty())
            emit dataChanged(index(0), index(static_cast<int>(items_.size()) - 1),
                {CountRole, LabelRole, Qt::DisplayRole});
    }
}

std::optional<QString> OrganizerFilterModel::key_at(int row) const {
    if (row < 0 || row >= items_.size()) return std::nullopt;
    return items_[row].key;
}

organizer::TreeFilter OrganizerFilterModel::for_key(const QString& key) const {
    for (const auto& entry : items_) if (entry.key == key) return entry;
    if (!items_.isEmpty()) return items_.first();
    return {QStringLiteral("all"), QStringLiteral("All"), 0};
}

}  // namespace px::modrim
