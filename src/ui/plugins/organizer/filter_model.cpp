#include "ui/plugins/organizer/filter_model.hpp"

namespace px::modrim {

OrganizerFilterModel::OrganizerFilterModel(QObject* parent)
    : QAbstractListModel(parent) {}

int OrganizerFilterModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant OrganizerFilterModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
    return {};
  const auto& entry = m_items[index.row()];
  const bool tag = entry.tagId.has_value() || entry.key.startsWith("tag:");
  switch (role) {
    case Qt::DisplayRole:
    case kLabelRole:
      return entry.label;
    case kCountRole:
      return entry.count;
    case kKeyRole:
      return entry.key;
    case kSectionRole:
      if (tag) return QStringLiteral("Tags");
      if (entry.providerId || entry.key.startsWith("provider:"))
        return QStringLiteral("Providers");
      return QStringLiteral("Status");
    case kIconRole:
      if (tag) return QStringLiteral("tag");
      if (entry.key == "all") return QStringLiteral("grid");
      if (entry.key == "active") return QStringLiteral("check-circle");
      if (entry.key == "inactive") return QStringLiteral("ban");
      if (entry.providerId == "steam" || entry.providerId == "downloaded")
        return QStringLiteral("steam");
      if (entry.providerId == "core") return QStringLiteral("grid");
      if (entry.providerId == "git") return QStringLiteral("git");
      return QStringLiteral("folder");
    default:
      return {};
  }
}

QHash<int, QByteArray> OrganizerFilterModel::roleNames() const {
  return {{Qt::DisplayRole, "filterLabel"},
          {kCountRole, "count"},
          {kIconRole, "iconName"},
          {kSectionRole, "sectionName"},
          {kKeyRole, "key"}};
}

void OrganizerFilterModel::update(const QList<organizer::TreeFilter>& entries) {
  bool sameKeys = entries.size() == m_items.size();
  for (qsizetype i = 0; sameKeys && i < entries.size(); ++i)
    sameKeys = entries[i].key == m_items[i].key;
  if (!sameKeys) {
    beginResetModel();
    m_items = entries;
    endResetModel();
  } else {
    m_items = entries;
    if (!m_items.isEmpty())
      emit dataChanged(index(0), index(static_cast<int>(m_items.size()) - 1),
                       {kCountRole, kLabelRole, Qt::DisplayRole});
  }
}

std::optional<QString> OrganizerFilterModel::keyAt(int row) const {
  if (row < 0 || row >= m_items.size()) return std::nullopt;
  return m_items[row].key;
}

organizer::TreeFilter OrganizerFilterModel::forKey(const QString& key) const {
  for (const auto& entry : m_items)
    if (entry.key == key) return entry;
  if (!m_items.isEmpty()) return m_items.first();
  return {QStringLiteral("all"), QStringLiteral("All"), 0};
}

}  // namespace px::modrim
