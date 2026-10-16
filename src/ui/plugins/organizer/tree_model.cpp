#include "ui/plugins/organizer/tree_model.h"

#include <algorithm>

#include "ui/models/mod_list_model.h"
#include "ui/theme/palette.h"

namespace px::modrim {
namespace {
using namespace organizer;

QString provider_icon(const QString& id) {
    if (id == "steam" || id == "downloaded") return QStringLiteral("steam");
    if (id == "core") return QStringLiteral("grid");
    if (id == "git") return QStringLiteral("git");
    return QStringLiteral("folder");
}

int check_state(CheckState state) {
    switch (state) {
        case CheckState::On: return Qt::Checked;
        case CheckState::Partial: return Qt::PartiallyChecked;
        case CheckState::Off: return Qt::Unchecked;
    }
    return Qt::Unchecked;
}

std::unique_ptr<TreeNode> build(const FolderNode& root, const ModsByUuid& mods, bool filtering) {
    auto top = std::make_unique<TreeNode>();
    top->kind = QStringLiteral("root");
    auto add_mods = [&](TreeNode* parent, const QList<ModLeaf>& leaves) {
        for (const auto& leaf : leaves) {
            auto item = std::make_unique<TreeNode>();
            item->kind = QStringLiteral("mod");
            item->key = "m:" + leaf.uuid;
            item->leaf = &leaf;
            const auto it = mods.constFind(leaf.uuid);
            if (it != mods.cend() && *it) {
                const auto& mod = **it;
                item->provider_id = mod.provider_id;
                item->provider_label = provider_label(mod.provider_id, mod_package_id(mod).value_or(QString{}));
                if (const auto* about = dynamic_cast<const AboutXmlMod*>(&mod))
                    item->mod_version = about->mod_version;
            }
            parent->add(std::move(item));
        }
    };
    auto add_folder = [&](auto&& self, TreeNode* parent, const FolderNode& folder) -> void {
        auto item = std::make_unique<TreeNode>();
        item->kind = QStringLiteral("folder");
        item->key = "f:" + QString::number(folder.folder.id);
        item->folder = &folder;
        TreeNode* ptr = item.get();
        parent->add(std::move(item));
        for (const auto& child : folder.children) self(self, ptr, child);
        add_mods(ptr, folder.mods);
    };
    for (const auto& child : root.children) add_folder(add_folder, top.get(), child);
    if (!root.mods.isEmpty() || !filtering) {
        auto item = std::make_unique<TreeNode>();
        item->kind = QStringLiteral("ungrouped");
        item->key = UNGROUPED_KEY;
        item->folder = &root;
        add_mods(item.get(), root.mods);
        top->add(std::move(item));
    }
    return top;
}

bool same_shape(const TreeNode& a, const TreeNode& b) {
    if (a.key != b.key || a.children.size() != b.children.size()) return false;
    for (size_t i = 0; i < a.children.size(); ++i)
        if (!same_shape(*a.children[i], *b.children[i])) return false;
    return true;
}

void copy_payload(TreeNode& dst, TreeNode& src) {
    dst.folder = src.folder;
    dst.leaf = src.leaf;
    dst.provider_id = std::move(src.provider_id);
    dst.provider_label = std::move(src.provider_label);
    dst.mod_version = std::move(src.mod_version);
    for (size_t i = 0; i < dst.children.size(); ++i)
        copy_payload(*dst.children[i], *src.children[i]);
}

const QList<int> diagnostic_roles{ModTreeModel::HasErrorRole, ModTreeModel::ErrorTooltipRole,
    ModTreeModel::HasWarningRole, ModTreeModel::WarningTooltipRole};
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
    const organizer::FolderNode* folder, const organizer::ModLeaf* leaf,
    QString provider_id)
    : kind(std::move(kind)), key(std::move(key)), parent(parent), folder(folder), leaf(leaf),
      provider_id(std::move(provider_id)) {}


qint64 TreeNode::folder_id() const {
    if (kind == "folder" && folder) return folder->folder.id;
    if (kind == "mod" && parent) return parent->folder_id();
    return organizer::ROOT_ID;
}

bool TreeNode::is_container() const { return kind == "folder" || kind == "ungrouped"; }

ModTreeModel::ModTreeModel(SummaryFor summary_for, QObject* parent)
    : QAbstractItemModel(parent), summary_for_(std::move(summary_for)), root_(std::make_unique<TreeNode>()) {
    root_->kind = QStringLiteral("root");
}

TreeNode* ModTreeModel::node(const QModelIndex& index) const {
    return index.isValid() ? static_cast<TreeNode*>(index.internalPointer()) : root_.get();
}

QModelIndex ModTreeModel::index(int row, int column, const QModelIndex& parent) const {
    const auto* current = node(parent);
    if (column != 0 || row < 0 || static_cast<size_t>(row) >= current->children.size()) return {};
    return createIndex(row, 0, current->children[row].get());
}

QModelIndex ModTreeModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) return {};
    const auto* up = node(child)->parent;
    return !up || up == root_.get() ? QModelIndex{} : index_of(up);
}

int ModTreeModel::rowCount(const QModelIndex& parent) const {
    return parent.column() > 0 ? 0 : static_cast<int>(node(parent)->children.size());
}
int ModTreeModel::columnCount(const QModelIndex&) const { return 1; }

QVariant ModTreeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const auto* item = node(index);
    if (role == Qt::DisplayRole) {
        if (item->kind == "ungrouped") return QStringLiteral("Ungrouped");
        if (item->kind == "folder" && item->folder) return item->folder->folder.name;
        return item->leaf ? item->leaf->name : QString{};
    }
    if (role == KindRole) return item->kind;
    if (role == KeyRole) return item->key;
    if (role == SelectedRole) return selected_.contains(item->key);
    if (role == FilteringRole) return filtering_;
    if (item->kind != "mod") {
        if (!item->folder) return {};
        const auto& folder = *item->folder;
        const bool own = item->kind == "ungrouped";
        switch (role) {
            case CheckStateRole: return check_state(own ? folder.own_check() : folder.check);
            case EnabledCountRole: return own ? folder.own_enabled : folder.enabled;
            case TotalCountRole: return own ? folder.own_total : folder.total;
            case VisibleCountRole: return own ? static_cast<int>(folder.mods.size()) : folder.visible_total;
            case HasRuleRole: return folder.has_rules && !own;
            default: return {};
        }
    }
    if (!item->leaf) return {};
    const auto& leaf = *item->leaf;
    switch (role) {
        case UuidRole: return leaf.uuid;
        case PackageIdRole: return leaf.package_id.value_or(QString{});
        case CheckStateRole: return leaf.enabled ? Qt::Checked : Qt::Unchecked;
        case ProviderLabelRole: return item->provider_label;
        case ModVersionRole: return item->mod_version;
        case ProviderIconRole: return provider_icon(item->provider_id);
        case ProviderColorRole: {
            const auto it = provider_colors_.constFind(item->provider_id);
            return it != provider_colors_.cend() ? *it : paletteValue(u"TEXT_DIM");
        }
        case CanPlaceRole: return leaf.package_id.has_value();
        case TagsRole: return tag_rows_.value(leaf.package_id.value_or(QString{}));
        default: break;
    }
    if (diagnostic_roles.contains(role)) {
        const auto summary = leaf.enabled ? summary_for_(leaf.uuid) : std::nullopt;
        if (role == HasErrorRole) return summary && summary->has_errors;
        if (role == HasWarningRole) return summary && summary->has_warnings;
        if (role == ErrorTooltipRole) return summary ? summary->error_tooltip : QString{};
        return summary ? summary->warning_tooltip : QString{};
    }
    return {};
}

QHash<int, QByteArray> ModTreeModel::roleNames() const {
    return {{Qt::DisplayRole, "name"}, {KindRole, "kind"}, {KeyRole, "key"},
        {UuidRole, "uuid"}, {PackageIdRole, "packageId"}, {CheckStateRole, "checkState"},
        {EnabledCountRole, "enabledCount"}, {TotalCountRole, "totalCount"},
        {VisibleCountRole, "visibleCount"}, {FilteringRole, "filtering"}, {HasRuleRole, "hasRule"},
        {ProviderIconRole, "providerIcon"}, {ProviderLabelRole, "providerLabel"},
        {ModVersionRole, "modVersion"}, {ProviderColorRole, "providerColor"},
        {HasErrorRole, "hasError"}, {ErrorTooltipRole, "errorTooltip"},
        {HasWarningRole, "hasWarning"}, {WarningTooltipRole, "warningTooltip"},
        {CanPlaceRole, "canPlace"}, {SelectedRole, "selected"}, {TagsRole, "tags"}};
}

bool ModTreeModel::filtering() const { return filtering_; }

void ModTreeModel::set_tree(const organizer::FolderNode& root,
    const organizer::ModsByUuid& mods, const QHash<QString, QString>& provider_colors,
    bool filtering, const QHash<qint64, organizer::Tag>& tags) {
    auto payload = std::make_unique<organizer::FolderNode>(root);
    auto fresh = build(*payload, mods, filtering);
    provider_colors_ = provider_colors;
    tag_rows_.clear();
    for (const auto* item : fresh->walk()) {
        if (!item->leaf || !item->leaf->package_id || item->leaf->package_id->isEmpty() || item->leaf->tag_ids.isEmpty()) continue;
        QList<qint64> ids(item->leaf->tag_ids.begin(), item->leaf->tag_ids.end());
        std::sort(ids.begin(), ids.end());
        QVariantList rows;
        for (qint64 id : ids) {
            const auto tag = tags.constFind(id);
            if (tag != tags.cend()) rows.append(QVariantMap{{"name", tag->name}, {"color", tag->color}});
        }
        tag_rows_.insert(*item->leaf->package_id, rows);
    }
    if (filtering_ == filtering && same_shape(*root_, *fresh)) {
        copy_payload(*root_, *fresh);
        payload_ = std::move(payload);
        emit_all_changed(root_.get());
    } else {
        beginResetModel();
        root_ = std::move(fresh);
        payload_ = std::move(payload);
        filtering_ = filtering;
        endResetModel();
    }
    QSet<QString> live;
    for (const auto* item : root_->walk()) live.insert(item->key);
    selected_.intersect(live);
}

QModelIndex ModTreeModel::index_of(const TreeNode* item) const {
    return item == root_.get() ? QModelIndex{} : createIndex(item->row, 0, const_cast<TreeNode*>(item));
}

void ModTreeModel::emit_all_changed(TreeNode* item, const QList<int>& roles) {
    if (item->children.empty()) return;
    const auto parent = index_of(item);
    emit dataChanged(index(0, 0, parent), index(static_cast<int>(item->children.size()) - 1, 0, parent), roles);
    for (const auto& child : item->children) emit_all_changed(child.get(), roles);
}

void ModTreeModel::refresh_diagnostics() { emit_all_changed(root_.get(), diagnostic_roles); }
TreeNode* ModTreeModel::node_at(const QModelIndex& index) const { return index.isValid() ? node(index) : nullptr; }
QModelIndex ModTreeModel::index_for_key(const QString& key) const {
    for (const auto* item : root_->walk()) if (item->key == key) return index_of(item);
    return {};
}
QModelIndex ModTreeModel::index_for_uuid(const QString& uuid) const {
    for (const auto* item : root_->walk())
        if (item->kind == "mod" && item->leaf && item->leaf->uuid == uuid) return index_of(item);
    return {};
}
QList<QModelIndex> ModTreeModel::expanded_indexes(const QModelIndex& parent) const {
    QList<QModelIndex> result;
    auto visit = [&](auto&& self, const TreeNode* item) -> void {
        for (const auto& child : item->children) {
            if (child->is_container() && (filtering_ || (child->folder && !child->folder->folder.collapsed))) {
                result.append(index_of(child.get()));
                self(self, child.get());
            }
        }
    };
    visit(visit, node(parent));
    return result;
}
QList<TreeNode*> ModTreeModel::selected_nodes() const {
    QList<TreeNode*> result;
    for (auto* item : root_->walk()) if (selected_.contains(item->key)) result.append(item);
    return result;
}
bool ModTreeModel::is_selected(const QModelIndex& index) const {
    const auto* item = node_at(index);
    return item && selected_.contains(item->key);
}
void ModTreeModel::select(const QList<QModelIndex>& indexes, const QString& mode) {
    QSet<QString> keys;
    for (const auto& index : indexes) if (const auto* item = node_at(index)) keys.insert(item->key);
    const auto before = selected_;
    if (mode == "replace") selected_ = keys;
    else if (mode == "toggle") selected_ = (selected_ - keys) | (keys - selected_);
    else selected_.unite(keys);
    notify_selection((before - selected_) | (selected_ - before));
}
void ModTreeModel::clear_selection() {
    const auto before = selected_;
    selected_.clear();
    notify_selection(before);
}
void ModTreeModel::notify_selection(const QSet<QString>& changed) {
    if (changed.isEmpty()) return;
    for (const auto* item : root_->walk()) if (changed.contains(item->key)) {
        const auto index = index_of(item);
        emit dataChanged(index, index, {SelectedRole});
    }
}

}  // namespace px::modrim
