#include "ui/models/mod_list_model.h"
#include "core/services/diagnostics_service.h"
#include "ui/models/impact.h"
#include "ui/theme/palette.h"
#include <algorithm>
#include <utility>

namespace px::modrim {
QString provider_label(const QString& provider_id, const QString& package_id) {
    const auto pid = provider_id.trimmed().toLower();
    const auto pkg = package_id.trimmed().toLower();
    if (pid == "steam" || pid == "downloaded") return QStringLiteral("Workshop");
    if (pid == "local") return QStringLiteral("Local");
    if (pid == "core") return !pkg.isEmpty() && pkg != "ludeon.rimworld" ? QStringLiteral("DLC") : QStringLiteral("Core");
    if (pkg.startsWith("ludeon.rimworld.")) return QStringLiteral("DLC");
    if (pkg == "ludeon.rimworld") return QStringLiteral("Core");
    return pid.isEmpty() ? pid : pid.first(1).toUpper() + pid.sliced(1);
}
QString section_name(bool checked, int active_count, int inactive_count) {
    return checked || active_count == 0 || inactive_count == 0 ? QString{} : QStringLiteral("Inactive (%1)").arg(inactive_count);
}
ModListModel::ModListModel(QHash<QString, QString> colors, DiagnosticsService* diagnostics, QObject* parent)
    : QAbstractListModel(parent), provider_colors_(std::move(colors)), diagnostics_(diagnostics) {
    if (diagnostics_) diagnostics_connection_ = diagnostics_->diagnostics_summary_changed.connect(
        [this](const auto& summaries) { diagnostics_changed(summaries); });
}
ModListModel::~ModListModel() {
    if (diagnostics_) diagnostics_->diagnostics_summary_changed.disconnect(diagnostics_connection_);
}
int ModListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(items_.size()); }
const ModItem* ModListModel::get_item(int row) const { return row >= 0 && row < items_.size() ? &items_[row] : nullptr; }
const ModItem* ModListModel::get_item_by_uuid(const QString& uuid) const {
    for (const auto& item : items_) if (item.uuid == uuid) return &item;
    return nullptr;
}
QVariant ModListModel::data(const QModelIndex& index, int role) const {
    const auto* item = index.isValid() ? get_item(index.row()) : nullptr;
    if (!item) return {};
    const auto* about = dynamic_cast<const AboutXmlMod*>(item->mod.get());
    switch (role) {
    case Qt::DisplayRole: return item->mod->name;
    case Qt::ToolTipRole: return item->mod->description;
    case CheckStateRole: return static_cast<int>(item->checked ? Qt::Checked : Qt::Unchecked);
    case PackageIdRole: return about ? about->package_id.str() : QString{};
    case ModVersionRole: return about ? about->mod_version : QString{};
    case ProviderColorRole: return item->provider_color;
    case UuidRole: return item->uuid;
    case StartupImpactRole: return item->startup_impact_s;
    case StartupImpactColorRole: return impact_color(item->startup_impact_s);
    case StartupImpactTextRole: return format_duration(item->startup_impact_s);
    case LoadIndexRole: return item->load_index;
    case IsActiveRole: return item->checked;
    case SectionNameRole: return section_name(item->checked, active_count_, static_cast<int>(items_.size()) - active_count_);
    case ProviderLabelRole: return item->provider_label;
    case HasErrorRole: case HasWarningRole: case ErrorTooltipRole: case WarningTooltipRole: {
        const auto diag = diagnostics_ ? diagnostics_->summary_for(item->uuid) : std::nullopt;
        if (role == HasErrorRole) return diag && diag->has_errors ? 1 : 0;
        if (role == HasWarningRole) return diag && diag->has_warnings ? 1 : 0;
        if (role == ErrorTooltipRole) return diag ? diag->error_tooltip : QString{};
        return diag ? diag->warning_tooltip : QString{};
    }
    default: return {};
    }
}
bool ModListModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || !get_item(index.row()) || role != CheckStateRole) return false;
    set_check_states({index.row()}, value.toInt() == Qt::Checked);
    return true;
}
Qt::ItemFlags ModListModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::ItemIsDropEnabled;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable;
}
QHash<int, QByteArray> ModListModel::roleNames() const {
    return {{Qt::DisplayRole,"name"},{CheckStateRole,"checkState"},{PackageIdRole,"packageId"},
        {ModVersionRole,"modVersion"},{ProviderColorRole,"providerColor"},{UuidRole,"uuid"},
        {HasErrorRole,"hasError"},{HasWarningRole,"hasWarning"},{ErrorTooltipRole,"errorTooltip"},
        {WarningTooltipRole,"warningTooltip"},{StartupImpactRole,"startupImpact"},{LoadIndexRole,"loadIndex"},
        {IsActiveRole,"isActive"},{SectionNameRole,"sectionName"},{ProviderLabelRole,"providerLabel"},
        {StartupImpactColorRole,"startupImpactColor"},{StartupImpactTextRole,"startupImpactText"},{Qt::ToolTipRole,"toolTip"}};
}
void ModListModel::update_provider_colors(QHash<QString, QString> colors) { provider_colors_ = std::move(colors); }
ModItem ModListModel::make_item(const QString& uuid, ListedModPtr mod, bool checked) const {
    const auto* about = dynamic_cast<const AboutXmlMod*>(mod.get());
    auto label = provider_label(mod->provider_id, about ? about->package_id.str() : QString{});
    auto color = provider_colors_.value(mod->provider_id, paletteValue(u"TEXT_DIM"));
    return {std::move(mod), uuid, checked, std::move(color), 0, -1, std::move(label)};
}
void ModListModel::load_mods(const QHash<QString, ListedModPtr>& mods, const QStringList& active_uuids) {
    beginResetModel();
    items_.clear();
    items_.reserve(mods.size());
    const QSet<QString> active(active_uuids.begin(), active_uuids.end());
    for (const auto& uuid : active_uuids) {
        const auto it = mods.constFind(uuid);
        if (it != mods.cend()) items_.append(make_item(uuid, it.value(), true));
    }
    QList<ModItem> inactive;
    inactive.reserve(mods.size() - items_.size());
    for (auto it = mods.cbegin(); it != mods.cend(); ++it)
        if (!active.contains(it.key())) inactive.append(make_item(it.key(), it.value(), false));
    std::stable_sort(inactive.begin(), inactive.end(), [](const auto& a, const auto& b) { return a.mod->name.toLower() < b.mod->name.toLower(); });
    items_.append(std::move(inactive));
    update_load_indices();
    endResetModel();
}
QStringList ModListModel::active_uuids() const {
    QStringList result;
    result.reserve(active_count_);
    for (const auto& item : items_) if (item.checked) result.append(item.uuid);
    return result;
}
QList<int> ModListModel::update_load_indices() {
    QList<int> changed;
    int count = 0;
    for (int row = 0; row < items_.size(); ++row) {
        auto& item = items_[row];
        const int index = item.checked ? ++count : -1;
        if (item.load_index != index) { item.load_index = index; changed.append(row); }
    }
    if (active_count_ != count) { active_count_ = count; emit active_count_changed(); }
    return changed;
}
bool ModListModel::move_row(int source, int target) {
    if (source == target || !get_item(source) || !get_item(target)) return false;
    beginMoveRows({}, source, source, {}, source < target ? target + 1 : target);
    items_.move(source, target);
    endMoveRows();
    const auto changed = update_load_indices();
    if (!changed.isEmpty()) emit dataChanged(index(changed.first()), index(changed.last()), {LoadIndexRole,SectionNameRole});
    return true;
}
void ModListModel::commitOrder(const QStringList& uuids) {
    QHash<QString, int> old_rows;
    for (int row = 0; row < items_.size(); ++row) old_rows.insert(items_[row].uuid, row);
    QList<int> order;
    order.reserve(items_.size());
    QSet<QString> ordered;
    for (const auto& uuid : uuids) if (old_rows.contains(uuid)) { order.append(old_rows.value(uuid)); ordered.insert(uuid); }
    QList<int> inactive;
    for (int row = 0; row < items_.size(); ++row) if (!ordered.contains(items_[row].uuid)) {
        if (items_[row].checked) order.append(row); else inactive.append(row);
    }
    std::stable_sort(inactive.begin(), inactive.end(), [this](int a, int b) { return items_[a].mod->name.toLower() < items_[b].mod->name.toLower(); });
    order.append(inactive);
    bool same = order.size() == items_.size();
    for (int i = 0; same && i < order.size(); ++i) same = order[i] == i;
    if (same) return;
    emit layoutAboutToBeChanged();
    const auto persistent = persistentIndexList();
    QList<ModItem> next;
    next.reserve(order.size());
    QHash<int,int> new_rows;
    for (int row = 0; row < order.size(); ++row) { new_rows.insert(order[row], row); next.append(items_[order[row]]); }
    items_ = std::move(next);
    update_load_indices();
    QModelIndexList replacements;
    replacements.reserve(persistent.size());
    for (const auto& previous : persistent) replacements.append(index(new_rows.value(previous.row(), -1)));
    changePersistentIndexList(persistent, replacements);
    emit layoutChanged();
}
void ModListModel::set_checkable(const QSet<QString>& uuids, bool checked) {
    QList<int> rows;
    for (int row = 0; row < items_.size(); ++row)
        if (uuids.contains(items_[row].uuid) && items_[row].checked != checked) rows.append(row);
    set_check_states(rows, checked);
}
void ModListModel::set_check_states(const QList<int>& rows, bool checked) {
    int first = static_cast<int>(items_.size());
    for (int row : rows) if (get_item(row) && items_[row].checked != checked) {
        items_[row].checked = checked; first = std::min(first, row);
    }
    if (first == items_.size()) return;
    const auto load_changed = update_load_indices();
    if (!load_changed.isEmpty()) first = std::min(first, load_changed.first());
    emit dataChanged(index(first), index(rowCount()-1), {CheckStateRole,LoadIndexRole,IsActiveRole,SectionNameRole});
    emit active_mods_changed();
}
void ModListModel::set_startup_impact(const QHash<QString, double>& impacts) {
    if (items_.isEmpty()) return;
    for (auto& item : items_) {
        const auto* about = dynamic_cast<const AboutXmlMod*>(item.mod.get());
        item.startup_impact_s = about ? impacts.value(about->package_id.str()) : 0;
    }
    emit dataChanged(index(0), index(rowCount()-1), {StartupImpactRole,StartupImpactColorRole,StartupImpactTextRole});
}
void ModListModel::diagnostics_changed(const QHash<QString, view::ModDiagnosticsView>& summaries) {
    if (items_.isEmpty()) return;
    int first = rowCount(), last = -1;
    for (int row = 0; row < items_.size(); ++row) {
        const auto& uuid = items_[row].uuid;
        if (last_diagnostics_.contains(uuid) != summaries.contains(uuid) || last_diagnostics_.value(uuid) != summaries.value(uuid)) {
            first = std::min(first, row); last = row;
        }
    }
    last_diagnostics_ = summaries;
    if (last >= 0) emit dataChanged(index(first), index(last), {HasErrorRole,HasWarningRole,ErrorTooltipRole,WarningTooltipRole});
}
}
