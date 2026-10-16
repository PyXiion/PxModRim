#include "ui/plugins/organizer/tree_model.hpp"

#include <algorithm>

#include "ui/models/mod_list_model.hpp"
#include "ui/theme/palette.hpp"

namespace px::modrim {
namespace {
using namespace organizer;

QString providerIcon(const QString& id) {
  if (id == "steam" || id == "downloaded") return QStringLiteral("steam");
  if (id == "core") return QStringLiteral("grid");
  if (id == "git") return QStringLiteral("git");
  return QStringLiteral("folder");
}

int checkState(CheckState state) {
  switch (state) {
    case CheckState::kOn:
      return Qt::Checked;
    case CheckState::kPartial:
      return Qt::PartiallyChecked;
    case CheckState::kOff:
      return Qt::Unchecked;
  }
  return Qt::Unchecked;
}

std::unique_ptr<TreeNode> build(const FolderNode& root, const ModsByUuid& mods,
                                bool filtering) {
  auto top = std::make_unique<TreeNode>();
  top->kind = QStringLiteral("root");
  auto addMods = [&](TreeNode* parent, const QList<ModLeaf>& leaves) {
    for (const auto& leaf : leaves) {
      auto item = std::make_unique<TreeNode>();
      item->kind = QStringLiteral("mod");
      item->key = "m:" + leaf.uuid;
      item->leaf = &leaf;
      const auto it = mods.constFind(leaf.uuid);
      if (it != mods.cend() && *it) {
        const auto& mod = **it;
        item->providerId = mod.providerId;
        item->providerLabel = providerLabel(
            mod.providerId, modPackageId(mod).value_or(QString{}));
        if (const auto* about = dynamic_cast<const AboutXmlMod*>(&mod))
          item->modVersion = about->modVersion;
      }
      parent->add(std::move(item));
    }
  };
  auto addFolder = [&](auto&& self, TreeNode* parent,
                       const FolderNode& folder) -> void {
    auto item = std::make_unique<TreeNode>();
    item->kind = QStringLiteral("folder");
    item->key = "f:" + QString::number(folder.folder.id);
    item->folder = &folder;
    TreeNode* ptr = item.get();
    parent->add(std::move(item));
    for (const auto& child : folder.children) self(self, ptr, child);
    addMods(ptr, folder.mods);
  };
  for (const auto& child : root.children)
    addFolder(addFolder, top.get(), child);
  if (!root.mods.isEmpty() || !filtering) {
    auto item = std::make_unique<TreeNode>();
    item->kind = QStringLiteral("ungrouped");
    item->key = kUngroupedKey;
    item->folder = &root;
    addMods(item.get(), root.mods);
    top->add(std::move(item));
  }
  return top;
}

bool sameShape(const TreeNode& a, const TreeNode& b) {
  if (a.key != b.key || a.children.size() != b.children.size()) return false;
  for (size_t i = 0; i < a.children.size(); ++i)
    if (!sameShape(*a.children[i], *b.children[i])) return false;
  return true;
}

void copyPayload(TreeNode& dst, TreeNode& src) {
  dst.folder = src.folder;
  dst.leaf = src.leaf;
  dst.providerId = std::move(src.providerId);
  dst.providerLabel = std::move(src.providerLabel);
  dst.modVersion = std::move(src.modVersion);
  for (size_t i = 0; i < dst.children.size(); ++i)
    copyPayload(*dst.children[i], *src.children[i]);
}

const QList<int> kDiagnosticRoles{
    ModTreeModel::kHasErrorRole, ModTreeModel::kErrorTooltipRole,
    ModTreeModel::kHasWarningRole, ModTreeModel::kWarningTooltipRole};
}  // namespace

void TreeNode::add(std::unique_ptr<TreeNode> child) {
  child->parent = this;
  child->row = static_cast<int>(children.size());
  children.push_back(std::move(child));
}

QList<TreeNode*> TreeNode::walk() const {
  QList<TreeNode*> result;
  auto visit = [&](auto&& self, const TreeNode& current) -> void {
    for (const auto& child : current.children) {
      result.append(child.get());
      self(self, *child);
    }
  };
  visit(visit, *this);
  return result;
}
TreeNode::TreeNode(QString kind, QString key, TreeNode* parent,
                   const organizer::FolderNode* folder,
                   const organizer::ModLeaf* leaf, QString providerId)
    : kind(std::move(kind)),
      key(std::move(key)),
      parent(parent),
      folder(folder),
      leaf(leaf),
      providerId(std::move(providerId)) {}

qint64 TreeNode::folderId() const {
  if (kind == "folder" && folder) return folder->folder.id;
  if (kind == "mod" && parent) return parent->folderId();
  return organizer::kRootId;
}

bool TreeNode::isContainer() const {
  return kind == "folder" || kind == "ungrouped";
}

ModTreeModel::ModTreeModel(SummaryFor summaryFor, QObject* parent)
    : QAbstractItemModel(parent),
      m_summaryFor(std::move(summaryFor)),
      m_root(std::make_unique<TreeNode>()) {
  m_root->kind = QStringLiteral("root");
}

TreeNode* ModTreeModel::node(const QModelIndex& index) const {
  return index.isValid() ? static_cast<TreeNode*>(index.internalPointer())
                         : m_root.get();
}

QModelIndex ModTreeModel::index(int row, int column,
                                const QModelIndex& parent) const {
  const auto* current = node(parent);
  if (column != 0 || row < 0 ||
      static_cast<size_t>(row) >= current->children.size())
    return {};
  return createIndex(row, 0, current->children[row].get());
}

QModelIndex ModTreeModel::parent(const QModelIndex& child) const {
  if (!child.isValid()) return {};
  const auto* up = node(child)->parent;
  return !up || up == m_root.get() ? QModelIndex{} : indexOf(up);
}

int ModTreeModel::rowCount(const QModelIndex& parent) const {
  return parent.column() > 0 ? 0
                             : static_cast<int>(node(parent)->children.size());
}
int ModTreeModel::columnCount(const QModelIndex&) const { return 1; }

QVariant ModTreeModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) return {};
  const auto* item = node(index);
  if (role == Qt::DisplayRole) {
    if (item->kind == "ungrouped") return QStringLiteral("Ungrouped");
    if (item->kind == "folder" && item->folder)
      return item->folder->folder.name;
    return item->leaf ? item->leaf->name : QString{};
  }
  if (role == kKindRole) return item->kind;
  if (role == kKeyRole) return item->key;
  if (role == kSelectedRole) return m_selected.contains(item->key);
  if (role == kFilteringRole) return m_filtering;
  if (item->kind != "mod") {
    if (!item->folder) return {};
    const auto& folder = *item->folder;
    const bool own = item->kind == "ungrouped";
    switch (role) {
      case kCheckStateRole:
        return checkState(own ? folder.ownCheck() : folder.check);
      case kEnabledCountRole:
        return own ? folder.ownEnabled : folder.enabled;
      case kTotalCountRole:
        return own ? folder.ownTotal : folder.total;
      case kVisibleCountRole:
        return own ? static_cast<int>(folder.mods.size()) : folder.visibleTotal;
      case kHasRuleRole:
        return folder.hasRules && !own;
      default:
        return {};
    }
  }
  if (!item->leaf) return {};
  const auto& leaf = *item->leaf;
  switch (role) {
    case kUuidRole:
      return leaf.uuid;
    case kPackageIdRole:
      return leaf.packageId.value_or(QString{});
    case kCheckStateRole:
      return leaf.enabled ? Qt::Checked : Qt::Unchecked;
    case kProviderLabelRole:
      return item->providerLabel;
    case kModVersionRole:
      return item->modVersion;
    case kProviderIconRole:
      return providerIcon(item->providerId);
    case kProviderColorRole: {
      const auto it = m_providerColors.constFind(item->providerId);
      return it != m_providerColors.cend() ? *it : paletteValue(u"TEXT_DIM");
    }
    case kCanPlaceRole:
      return leaf.packageId.has_value();
    case kTagsRole:
      return m_tagRows.value(leaf.packageId.value_or(QString{}));
    default:
      break;
  }
  if (kDiagnosticRoles.contains(role)) {
    const auto summary = leaf.enabled ? m_summaryFor(leaf.uuid) : std::nullopt;
    if (role == kHasErrorRole) return summary && summary->hasErrors;
    if (role == kHasWarningRole) return summary && summary->hasWarnings;
    if (role == kErrorTooltipRole)
      return summary ? summary->errorTooltip : QString{};
    return summary ? summary->warningTooltip : QString{};
  }
  return {};
}

QHash<int, QByteArray> ModTreeModel::roleNames() const {
  return {{Qt::DisplayRole, "name"},
          {kKindRole, "kind"},
          {kKeyRole, "key"},
          {kUuidRole, "uuid"},
          {kPackageIdRole, "packageId"},
          {kCheckStateRole, "checkState"},
          {kEnabledCountRole, "enabledCount"},
          {kTotalCountRole, "totalCount"},
          {kVisibleCountRole, "visibleCount"},
          {kFilteringRole, "filtering"},
          {kHasRuleRole, "hasRule"},
          {kProviderIconRole, "providerIcon"},
          {kProviderLabelRole, "providerLabel"},
          {kModVersionRole, "modVersion"},
          {kProviderColorRole, "providerColor"},
          {kHasErrorRole, "hasError"},
          {kErrorTooltipRole, "errorTooltip"},
          {kHasWarningRole, "hasWarning"},
          {kWarningTooltipRole, "warningTooltip"},
          {kCanPlaceRole, "canPlace"},
          {kSelectedRole, "selected"},
          {kTagsRole, "tags"}};
}

bool ModTreeModel::filtering() const { return m_filtering; }

void ModTreeModel::setTree(const organizer::FolderNode& root,
                           const organizer::ModsByUuid& mods,
                           const QHash<QString, QString>& providerColors,
                           bool filtering,
                           const QHash<qint64, organizer::Tag>& tags) {
  auto payload = std::make_unique<organizer::FolderNode>(root);
  auto fresh = build(*payload, mods, filtering);
  m_providerColors = providerColors;
  m_tagRows.clear();
  for (const auto* item : fresh->walk()) {
    if (!item->leaf || !item->leaf->packageId ||
        item->leaf->packageId->isEmpty() || item->leaf->tagIds.isEmpty())
      continue;
    QList<qint64> ids(item->leaf->tagIds.begin(), item->leaf->tagIds.end());
    std::sort(ids.begin(), ids.end());
    QVariantList rows;
    for (qint64 id : ids) {
      const auto tag = tags.constFind(id);
      if (tag != tags.cend())
        rows.append(QVariantMap{{"name", tag->name}, {"color", tag->color}});
    }
    m_tagRows.insert(*item->leaf->packageId, rows);
  }
  if (m_filtering == filtering && sameShape(*m_root, *fresh)) {
    copyPayload(*m_root, *fresh);
    m_payload = std::move(payload);
    emitAllChanged(m_root.get());
  } else {
    beginResetModel();
    m_root = std::move(fresh);
    m_payload = std::move(payload);
    m_filtering = filtering;
    endResetModel();
  }
  QSet<QString> live;
  for (const auto* item : m_root->walk()) live.insert(item->key);
  m_selected.intersect(live);
}

QModelIndex ModTreeModel::indexOf(const TreeNode* item) const {
  return item == m_root.get()
             ? QModelIndex{}
             : createIndex(item->row, 0, const_cast<TreeNode*>(item));
}

void ModTreeModel::emitAllChanged(TreeNode* item, const QList<int>& roles) {
  if (item->children.empty()) return;
  const auto parent = indexOf(item);
  emit dataChanged(
      index(0, 0, parent),
      index(static_cast<int>(item->children.size()) - 1, 0, parent), roles);
  for (const auto& child : item->children) emitAllChanged(child.get(), roles);
}

void ModTreeModel::refreshDiagnostics() {
  emitAllChanged(m_root.get(), kDiagnosticRoles);
}
TreeNode* ModTreeModel::nodeAt(const QModelIndex& index) const {
  return index.isValid() ? node(index) : nullptr;
}
QModelIndex ModTreeModel::indexForKey(const QString& key) const {
  auto find = [&](auto&& self, const TreeNode* parent) -> QModelIndex {
    for (const auto& child : parent->children) {
      if (child->key == key) return indexOf(child.get());
      const auto found = self(self, child.get());
      if (found.isValid()) return found;
    }
    return {};
  };
  return find(find, m_root.get());
}
QModelIndex ModTreeModel::indexForUuid(const QString& uuid) const {
  return indexForKey("m:" + uuid);
}
QList<QModelIndex> ModTreeModel::expandedIndexes(
    const QModelIndex& parent) const {
  QList<QModelIndex> result;
  auto visit = [&](auto&& self, const TreeNode* item) -> void {
    for (const auto& child : item->children) {
      if (child->isContainer() &&
          (m_filtering ||
           (child->folder && !child->folder->folder.collapsed))) {
        result.append(indexOf(child.get()));
        self(self, child.get());
      }
    }
  };
  visit(visit, node(parent));
  return result;
}
QList<TreeNode*> ModTreeModel::selectedNodes() const {
  QList<TreeNode*> result;
  for (auto* item : m_root->walk())
    if (m_selected.contains(item->key)) result.append(item);
  return result;
}
bool ModTreeModel::isSelected(const QModelIndex& index) const {
  const auto* item = nodeAt(index);
  return item && m_selected.contains(item->key);
}
void ModTreeModel::select(const QList<QModelIndex>& indexes,
                          const QString& mode) {
  QSet<QString> keys;
  for (const auto& index : indexes)
    if (const auto* item = nodeAt(index)) keys.insert(item->key);
  const auto before = m_selected;
  if (mode == "replace")
    m_selected = keys;
  else if (mode == "toggle")
    m_selected = (m_selected - keys) | (keys - m_selected);
  else
    m_selected.unite(keys);
  notifySelection((before - m_selected) | (m_selected - before));
}
void ModTreeModel::clearSelection() {
  const auto before = m_selected;
  m_selected.clear();
  notifySelection(before);
}
void ModTreeModel::notifySelection(const QSet<QString>& changed) {
  if (changed.isEmpty()) return;
  for (const auto* item : m_root->walk())
    if (changed.contains(item->key)) {
      const auto index = indexOf(item);
      emit dataChanged(index, index, {kSelectedRole});
    }
}

}  // namespace px::modrim
