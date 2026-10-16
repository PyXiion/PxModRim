#include "ui/models/mod_list_proxy_model.h"
#include <algorithm>
#include <utility>
namespace px::modrim {
ModListProxyModel::ModListProxyModel(ModListModel* source, QObject* parent) : QAbstractListModel(parent), source_(source) {
    connect(source_, &QAbstractItemModel::dataChanged, this, &ModListProxyModel::source_data_changed);
    connect(source_, &QAbstractItemModel::layoutChanged, this, [this] { if (!in_proxy_move_) layout_rebuild(); });
    connect(source_, &QAbstractItemModel::modelReset, this, [this] { beginResetModel(); rebuild(); endResetModel(); });
    connect(source_, &QAbstractItemModel::rowsMoved, this, [this] { if (!in_proxy_move_) layout_rebuild(); });
    connect(source_, &QAbstractItemModel::rowsInserted, this, [this] { if (!in_proxy_move_) layout_rebuild(); });
    connect(source_, &QAbstractItemModel::rowsRemoved, this, [this] { if (!in_proxy_move_) layout_rebuild(); });
    rebuild();
}
int ModListProxyModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(visible_uuids_.size()); }
void ModListProxyModel::rebuild_source_mapping() {
    source_rows_.clear();
    for (int row = 0; row < source_->rowCount(); ++row) source_rows_.insert(source_->get_item(row)->uuid, row);
}
bool ModListProxyModel::accept_item(const ModItem& item) const {
    if (sidebar_uuids_ && !sidebar_uuids_->contains(item.uuid)) return false;
    if (search_text_.isEmpty() || item.mod->name.trimmed().toCaseFolded().contains(search_text_)) return true;
    const auto* about = dynamic_cast<const AboutXmlMod*>(item.mod.get());
    return about && about->package_id.str().trimmed().toCaseFolded().contains(search_text_);
}
void ModListProxyModel::rebuild() {
    rebuild_source_mapping();
    visible_uuids_.clear(); proxy_rows_.clear();
    for (int row = 0; row < source_->rowCount(); ++row) {
        const auto& item = *source_->get_item(row);
        if (accept_item(item)) { proxy_rows_.insert(item.uuid, static_cast<int>(visible_uuids_.size())); visible_uuids_.append(item.uuid); }
    }
    update_visible_counts();
}
void ModListProxyModel::update_visible_counts() {
    int active = 0;
    for (const auto& uuid : visible_uuids_) {
        const auto* item = source_->get_item(source_rows_.value(uuid, -1));
        if (item && item->checked) ++active;
    }
    const bool changed = active != visible_active_count_;
    visible_active_count_ = active;
    visible_inactive_count_ = static_cast<int>(visible_uuids_.size()) - active;
    if (changed) emit active_count_changed();
}
int ModListProxyModel::proxy_row_for_uuid(const QString& uuid) const { return proxy_rows_.value(uuid, -1); }
QModelIndex ModListProxyModel::mapToSource(const QModelIndex& index) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= visible_uuids_.size()) return {};
    return source_->index(source_rows_.value(visible_uuids_[index.row()], -1));
}
QModelIndex ModListProxyModel::mapFromSource(const QModelIndex& source_index) const {
    const auto* item = source_index.isValid() ? source_->get_item(source_index.row()) : nullptr;
    return item ? index(proxy_row_for_uuid(item->uuid)) : QModelIndex{};
}
QVariant ModListProxyModel::data(const QModelIndex& proxy_index, int role) const {
    const auto source_index = mapToSource(proxy_index);
    if (!source_index.isValid()) return {};
    if (role == ModListModel::SectionNameRole) {
        return section_name(source_->get_item(source_index.row())->checked, visible_active_count_, visible_inactive_count_);
    }
    return source_->data(source_index, role);
}
bool ModListProxyModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    const auto source_index = mapToSource(index);
    return source_index.isValid() && source_->setData(source_index, value, role);
}
Qt::ItemFlags ModListProxyModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::ItemIsDropEnabled;
    const auto source_index = mapToSource(index);
    return source_index.isValid() ? source_->flags(source_index) : Qt::NoItemFlags;
}
QHash<int,QByteArray> ModListProxyModel::roleNames() const { return source_->roleNames(); }
void ModListProxyModel::layout_rebuild() {
    const auto persistent = persistentIndexList();
    QStringList uuids;
    uuids.reserve(persistent.size());
    for (const auto& index : persistent) uuids.append(index.isValid() && index.row() < visible_uuids_.size() ? visible_uuids_[index.row()] : QString{});
    emit layoutAboutToBeChanged();
    rebuild();
    QModelIndexList replacements;
    replacements.reserve(uuids.size());
    for (const auto& uuid : uuids) replacements.append(index(proxy_row_for_uuid(uuid)));
    changePersistentIndexList(persistent, replacements);
    emit layoutChanged();
}
void ModListProxyModel::set_search_filter(const QString& text) {
    const auto normalized = text.trimmed().toCaseFolded();
    if (normalized == search_text_) return;
    const bool was_filtered = is_filtered();
    search_text_ = normalized;
    layout_rebuild();
    if (was_filtered != is_filtered()) emit is_filtered_changed();
}
void ModListProxyModel::set_sidebar_filter(std::optional<QSet<QString>> uuids) {
    if (sidebar_uuids_ == uuids) return;
    const bool was_filtered = is_filtered();
    sidebar_uuids_ = std::move(uuids);
    layout_rebuild();
    if (was_filtered != is_filtered()) emit is_filtered_changed();
}
bool ModListProxyModel::move_row(int source, int target, bool clamp_to_active) {
    if (source < 0 || source >= visible_uuids_.size()) return false;
    const auto uuid = visible_uuids_[source];
    const int source_row = source_rows_.value(uuid, -1);
    if (clamp_to_active) {
        const auto* item = source_->get_item(source_row);
        if (is_filtered() || !item || !item->checked) return false;
        target = std::max(0, std::min(target, visible_active_count_ - 1));
    }
    if (source == target || target < 0 || target >= visible_uuids_.size()) return false;
    const int target_row = source_rows_.value(visible_uuids_[target], -1);
    if (source_row < 0 || target_row < 0) return false;
    // Source data signals refer to post-move rows, while the proxy is still moving.
    in_proxy_move_ = true;
    beginMoveRows({}, source, source, {}, source < target ? target + 1 : target);
    visible_uuids_.move(source, target);
    proxy_rows_.clear();
    for (int row = 0; row < visible_uuids_.size(); ++row) proxy_rows_.insert(visible_uuids_[row], row);
    source_->move_row(source_row, target_row);
    rebuild_source_mapping();
    endMoveRows();
    in_proxy_move_ = false;
    emit dataChanged(index(std::min(source,target)), index(std::max(source,target)), {ModListModel::LoadIndexRole,ModListModel::SectionNameRole});
    return true;
}
void ModListProxyModel::source_data_changed(const QModelIndex& top, const QModelIndex& bottom, const QList<int>& roles) {
    if (in_proxy_move_) return;
    if (roles.isEmpty() || roles.contains(ModListModel::CheckStateRole) || roles.contains(ModListModel::IsActiveRole)) update_visible_counts();
    int first = rowCount(), last = -1;
    for (int row = top.row(); row <= bottom.row(); ++row) {
        const auto* item = source_->get_item(row);
        if (!item) continue;
        const int proxy_row = proxy_row_for_uuid(item->uuid);
        if (proxy_row < 0) continue;
        first = std::min(first, proxy_row); last = std::max(last, proxy_row);
    }
    if (last >= 0) emit dataChanged(index(first), index(last), roles);
}
}
