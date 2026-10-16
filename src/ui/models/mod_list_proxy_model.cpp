#include "ui/models/mod_list_proxy_model.hpp"

#include <algorithm>
#include <utility>
namespace px::modrim {
ModListProxyModel::ModListProxyModel(ModListModel* source, QObject* parent)
    : QAbstractListModel(parent), m_source(source) {
  connect(m_source, &QAbstractItemModel::dataChanged, this,
          &ModListProxyModel::sourceDataChanged);
  const auto rebuildLayout = [this] {
    if (!m_inProxyMove) layoutRebuild();
  };
  connect(m_source, &QAbstractItemModel::layoutChanged, this, rebuildLayout);
  connect(m_source, &QAbstractItemModel::modelReset, this, [this] {
    beginResetModel();
    rebuild();
    endResetModel();
  });
  connect(m_source, &QAbstractItemModel::rowsMoved, this, rebuildLayout);
  connect(m_source, &QAbstractItemModel::rowsInserted, this, rebuildLayout);
  connect(m_source, &QAbstractItemModel::rowsRemoved, this, rebuildLayout);
  rebuild();
}
int ModListProxyModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_visibleUuids.size());
}
void ModListProxyModel::rebuildSourceMapping() {
  m_sourceRows.clear();
  for (int row = 0; row < m_source->rowCount(); ++row)
    m_sourceRows.insert(m_source->getItem(row)->uuid, row);
}
bool ModListProxyModel::acceptItem(const ModItem& item) const {
  if (m_sidebarUuids && !m_sidebarUuids->contains(item.uuid)) return false;
  if (m_searchText.isEmpty() ||
      item.mod->name.trimmed().toCaseFolded().contains(m_searchText))
    return true;
  const auto* about = dynamic_cast<const AboutXmlMod*>(item.mod.get());
  return about &&
         about->packageId.str().trimmed().toCaseFolded().contains(m_searchText);
}
void ModListProxyModel::rebuild() {
  rebuildSourceMapping();
  m_visibleUuids.clear();
  m_proxyRows.clear();
  for (int row = 0; row < m_source->rowCount(); ++row) {
    const auto& item = *m_source->getItem(row);
    if (acceptItem(item)) {
      m_proxyRows.insert(item.uuid, static_cast<int>(m_visibleUuids.size()));
      m_visibleUuids.append(item.uuid);
    }
  }
  updateVisibleCounts();
}
void ModListProxyModel::updateVisibleCounts() {
  int active = 0;
  for (const auto& uuid : m_visibleUuids) {
    const auto* item = m_source->getItem(m_sourceRows.value(uuid, -1));
    if (item && item->checked) ++active;
  }
  const bool changed = active != m_visibleActiveCount;
  m_visibleActiveCount = active;
  m_visibleInactiveCount = static_cast<int>(m_visibleUuids.size()) - active;
  if (changed) emit activeCountChanged();
}
int ModListProxyModel::proxyRowForUuid(const QString& uuid) const {
  return m_proxyRows.value(uuid, -1);
}
QModelIndex ModListProxyModel::mapToSource(const QModelIndex& index) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= m_visibleUuids.size())
    return {};
  return m_source->index(m_sourceRows.value(m_visibleUuids[index.row()], -1));
}
QModelIndex ModListProxyModel::mapFromSource(
    const QModelIndex& sourceIndex) const {
  const auto* item =
      sourceIndex.isValid() ? m_source->getItem(sourceIndex.row()) : nullptr;
  return item ? index(proxyRowForUuid(item->uuid)) : QModelIndex{};
}
QVariant ModListProxyModel::data(const QModelIndex& proxyIndex,
                                 int role) const {
  const auto sourceIndex = mapToSource(proxyIndex);
  if (!sourceIndex.isValid()) return {};
  if (role == ModListModel::kSectionNameRole) {
    return sectionName(m_source->getItem(sourceIndex.row())->checked,
                       m_visibleActiveCount, m_visibleInactiveCount);
  }
  return m_source->data(sourceIndex, role);
}
bool ModListProxyModel::setData(const QModelIndex& index, const QVariant& value,
                                int role) {
  const auto sourceIndex = mapToSource(index);
  return sourceIndex.isValid() && m_source->setData(sourceIndex, value, role);
}
Qt::ItemFlags ModListProxyModel::flags(const QModelIndex& index) const {
  if (!index.isValid()) return Qt::ItemIsDropEnabled;
  const auto sourceIndex = mapToSource(index);
  return sourceIndex.isValid() ? m_source->flags(sourceIndex) : Qt::NoItemFlags;
}
QHash<int, QByteArray> ModListProxyModel::roleNames() const {
  return m_source->roleNames();
}
void ModListProxyModel::layoutRebuild() {
  const auto persistent = persistentIndexList();
  QStringList uuids;
  uuids.reserve(persistent.size());
  for (const auto& index : persistent)
    uuids.append(index.isValid() && index.row() < m_visibleUuids.size()
                     ? m_visibleUuids[index.row()]
                     : QString{});
  emit layoutAboutToBeChanged();
  rebuild();
  QModelIndexList replacements;
  replacements.reserve(uuids.size());
  for (const auto& uuid : uuids)
    replacements.append(index(proxyRowForUuid(uuid)));
  changePersistentIndexList(persistent, replacements);
  emit layoutChanged();
}
void ModListProxyModel::setSearchFilter(const QString& text) {
  const auto normalized = text.trimmed().toCaseFolded();
  if (normalized == m_searchText) return;
  const bool wasFiltered = isFiltered();
  m_searchText = normalized;
  layoutRebuild();
  if (wasFiltered != isFiltered()) emit isFilteredChanged();
}
void ModListProxyModel::setSidebarFilter(std::optional<QSet<QString>> uuids) {
  if (m_sidebarUuids == uuids) return;
  const bool wasFiltered = isFiltered();
  m_sidebarUuids = std::move(uuids);
  layoutRebuild();
  if (wasFiltered != isFiltered()) emit isFilteredChanged();
}
bool ModListProxyModel::moveRow(int source, int target, bool clampToActive) {
  if (source < 0 || source >= m_visibleUuids.size()) return false;
  const auto uuid = m_visibleUuids[source];
  const int sourceRow = m_sourceRows.value(uuid, -1);
  if (clampToActive) {
    const auto* item = m_source->getItem(sourceRow);
    if (isFiltered() || !item || !item->checked) return false;
    target = std::max(0, std::min(target, m_visibleActiveCount - 1));
  }
  if (source == target || target < 0 || target >= m_visibleUuids.size())
    return false;
  const int targetRow = m_sourceRows.value(m_visibleUuids[target], -1);
  if (sourceRow < 0 || targetRow < 0) return false;
  // Source data signals refer to post-move rows, while the proxy is still
  // moving.
  m_inProxyMove = true;
  beginMoveRows({}, source, source, {}, source < target ? target + 1 : target);
  m_visibleUuids.move(source, target);
  m_proxyRows.clear();
  for (int row = 0; row < m_visibleUuids.size(); ++row)
    m_proxyRows.insert(m_visibleUuids[row], row);
  m_source->moveRow(sourceRow, targetRow);
  rebuildSourceMapping();
  endMoveRows();
  m_inProxyMove = false;
  emit dataChanged(
      index(std::min(source, target)), index(std::max(source, target)),
      {ModListModel::kLoadIndexRole, ModListModel::kSectionNameRole});
  return true;
}
void ModListProxyModel::sourceDataChanged(const QModelIndex& top,
                                          const QModelIndex& bottom,
                                          const QList<int>& roles) {
  if (m_inProxyMove) return;
  if (roles.isEmpty() || roles.contains(ModListModel::kCheckStateRole) ||
      roles.contains(ModListModel::kIsActiveRole))
    updateVisibleCounts();
  int first = rowCount(), last = -1;
  for (int row = top.row(); row <= bottom.row(); ++row) {
    const auto* item = m_source->getItem(row);
    if (!item) continue;
    const int proxyRow = proxyRowForUuid(item->uuid);
    if (proxyRow < 0) continue;
    first = std::min(first, proxyRow);
    last = std::max(last, proxyRow);
  }
  if (last >= 0) emit dataChanged(index(first), index(last), roles);
}
}  // namespace px::modrim
