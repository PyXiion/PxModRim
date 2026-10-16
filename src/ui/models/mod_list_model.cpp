#include "ui/models/mod_list_model.hpp"
#include "core/services/diagnostics_service.hpp"
#include "ui/models/impact.hpp"
#include "ui/theme/palette.hpp"
#include <algorithm>
#include <utility>

namespace px::modrim {
QString providerLabel(const QString& providerId, const QString& packageId) {
    const auto pid = providerId.trimmed().toLower();
    const auto pkg = packageId.trimmed().toLower();
    if (pid == "steam" || pid == "downloaded") return QStringLiteral("Workshop");
    if (pid == "local") return QStringLiteral("Local");
    if (pid == "core") return !pkg.isEmpty() && pkg != "ludeon.rimworld" ? QStringLiteral("DLC") : QStringLiteral("Core");
    if (pkg.startsWith("ludeon.rimworld.")) return QStringLiteral("DLC");
    if (pkg == "ludeon.rimworld") return QStringLiteral("Core");
    return pid.isEmpty() ? pid : pid.first(1).toUpper() + pid.sliced(1);
}
QString sectionName(bool checked, int activeCount, int inactiveCount) {
    return checked || activeCount == 0 || inactiveCount == 0 ? QString{} : QStringLiteral("Inactive (%1)").arg(inactiveCount);
}
ModListModel::ModListModel(QHash<QString, QString> colors, DiagnosticsService* diagnostics, QObject* parent)
    : QAbstractListModel(parent), m_providerColors(std::move(colors)), m_diagnostics(diagnostics) {
    if (m_diagnostics) m_diagnosticsConnection = m_diagnostics->diagnosticsSummaryChanged.connect(
        [this](const auto& summaries) { diagnosticsChanged(summaries); });
}
ModListModel::~ModListModel() {
    if (m_diagnostics) m_diagnostics->diagnosticsSummaryChanged.disconnect(m_diagnosticsConnection);
}
int ModListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(m_items.size()); }
const ModItem* ModListModel::getItem(int row) const { return row >= 0 && row < m_items.size() ? &m_items[row] : nullptr; }
const ModItem* ModListModel::getItemByUuid(const QString& uuid) const {
    for (const auto& item : m_items) if (item.uuid == uuid) return &item;
    return nullptr;
}
QVariant ModListModel::data(const QModelIndex& index, int role) const {
    const auto* item = index.isValid() ? getItem(index.row()) : nullptr;
    if (!item) return {};
    const auto* about = dynamic_cast<const AboutXmlMod*>(item->mod.get());
    switch (role) {
    case Qt::DisplayRole: return item->mod->name;
    case Qt::ToolTipRole: return item->mod->description;
    case kCheckStateRole: return static_cast<int>(item->checked ? Qt::Checked : Qt::Unchecked);
    case kPackageIdRole: return about ? about->packageId.str() : QString{};
    case kModVersionRole: return about ? about->modVersion : QString{};
    case kProviderColorRole: return item->providerColor;
    case kUuidRole: return item->uuid;
    case kStartupImpactRole: return item->startupImpactS;
    case kStartupImpactColorRole: return impactColor(item->startupImpactS);
    case kStartupImpactTextRole: return formatDuration(item->startupImpactS);
    case kLoadIndexRole: return item->loadIndex;
    case kIsActiveRole: return item->checked;
    case kSectionNameRole: return sectionName(item->checked, m_activeCount, static_cast<int>(m_items.size()) - m_activeCount);
    case kProviderLabelRole: return item->providerLabel;
    case kHasErrorRole: case kHasWarningRole: case kErrorTooltipRole: case kWarningTooltipRole: {
        const auto diag = m_diagnostics ? m_diagnostics->summaryFor(item->uuid) : std::nullopt;
        if (role == kHasErrorRole) return diag && diag->hasErrors ? 1 : 0;
        if (role == kHasWarningRole) return diag && diag->hasWarnings ? 1 : 0;
        if (role == kErrorTooltipRole) return diag ? diag->errorTooltip : QString{};
        return diag ? diag->warningTooltip : QString{};
    }
    default: return {};
    }
}
bool ModListModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || !getItem(index.row()) || role != kCheckStateRole) return false;
    setCheckStates({index.row()}, value.toInt() == Qt::Checked);
    return true;
}
Qt::ItemFlags ModListModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::ItemIsDropEnabled;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable;
}
QHash<int, QByteArray> ModListModel::roleNames() const {
    return {{Qt::DisplayRole,"name"},{kCheckStateRole,"checkState"},{kPackageIdRole,"packageId"},
        {kModVersionRole,"modVersion"},{kProviderColorRole,"providerColor"},{kUuidRole,"uuid"},
        {kHasErrorRole,"hasError"},{kHasWarningRole,"hasWarning"},{kErrorTooltipRole,"errorTooltip"},
        {kWarningTooltipRole,"warningTooltip"},{kStartupImpactRole,"startupImpact"},{kLoadIndexRole,"loadIndex"},
        {kIsActiveRole,"isActive"},{kSectionNameRole,"sectionName"},{kProviderLabelRole,"providerLabel"},
        {kStartupImpactColorRole,"startupImpactColor"},{kStartupImpactTextRole,"startupImpactText"},{Qt::ToolTipRole,"toolTip"}};
}
void ModListModel::updateProviderColors(QHash<QString, QString> colors) { m_providerColors = std::move(colors); }
ModItem ModListModel::makeItem(const QString& uuid, ListedModPtr mod, bool checked) const {
    const auto* about = dynamic_cast<const AboutXmlMod*>(mod.get());
    auto label = providerLabel(mod->providerId, about ? about->packageId.str() : QString{});
    auto color = m_providerColors.value(mod->providerId, paletteValue(u"TEXT_DIM"));
    return {std::move(mod), uuid, checked, std::move(color), 0, -1, std::move(label)};
}
void ModListModel::loadMods(const QHash<QString, ListedModPtr>& mods, const QStringList& activeUuids) {
    beginResetModel();
    m_items.clear();
    m_items.reserve(mods.size());
    const QSet<QString> active(activeUuids.begin(), activeUuids.end());
    for (const auto& uuid : activeUuids) {
        const auto it = mods.constFind(uuid);
        if (it != mods.cend()) m_items.append(makeItem(uuid, it.value(), true));
    }
    QList<ModItem> inactive;
    inactive.reserve(mods.size() - m_items.size());
    for (auto it = mods.cbegin(); it != mods.cend(); ++it)
        if (!active.contains(it.key())) inactive.append(makeItem(it.key(), it.value(), false));
    std::stable_sort(inactive.begin(), inactive.end(), [](const auto& a, const auto& b) { return a.mod->name.toLower() < b.mod->name.toLower(); });
    m_items.append(std::move(inactive));
    updateLoadIndices();
    endResetModel();
}
QStringList ModListModel::activeUuids() const {
    QStringList result;
    result.reserve(m_activeCount);
    for (const auto& item : m_items) if (item.checked) result.append(item.uuid);
    return result;
}
QList<int> ModListModel::updateLoadIndices() {
    QList<int> changed;
    int count = 0;
    for (int row = 0; row < m_items.size(); ++row) {
        auto& item = m_items[row];
        const int index = item.checked ? ++count : -1;
        if (item.loadIndex != index) { item.loadIndex = index; changed.append(row); }
    }
    if (m_activeCount != count) { m_activeCount = count; emit activeCountChanged(); }
    return changed;
}
bool ModListModel::moveRow(int source, int target) {
    if (source == target || !getItem(source) || !getItem(target)) return false;
    beginMoveRows({}, source, source, {}, source < target ? target + 1 : target);
    m_items.move(source, target);
    endMoveRows();
    const auto changed = updateLoadIndices();
    if (!changed.isEmpty()) emit dataChanged(index(changed.first()), index(changed.last()), {kLoadIndexRole,kSectionNameRole});
    return true;
}
void ModListModel::commitOrder(const QStringList& uuids) {
    QHash<QString, int> oldRows;
    for (int row = 0; row < m_items.size(); ++row) oldRows.insert(m_items[row].uuid, row);
    QList<int> order;
    order.reserve(m_items.size());
    QSet<QString> ordered;
    for (const auto& uuid : uuids) if (oldRows.contains(uuid)) { order.append(oldRows.value(uuid)); ordered.insert(uuid); }
    QList<int> inactive;
    for (int row = 0; row < m_items.size(); ++row) if (!ordered.contains(m_items[row].uuid)) {
        if (m_items[row].checked) order.append(row); else inactive.append(row);
    }
    std::stable_sort(inactive.begin(), inactive.end(), [this](int a, int b) { return m_items[a].mod->name.toLower() < m_items[b].mod->name.toLower(); });
    order.append(inactive);
    bool same = order.size() == m_items.size();
    for (int i = 0; same && i < order.size(); ++i) same = order[i] == i;
    if (same) return;
    emit layoutAboutToBeChanged();
    const auto persistent = persistentIndexList();
    QList<ModItem> next;
    next.reserve(order.size());
    QHash<int,int> newRows;
    for (int row = 0; row < order.size(); ++row) { newRows.insert(order[row], row); next.append(m_items[order[row]]); }
    m_items = std::move(next);
    updateLoadIndices();
    QModelIndexList replacements;
    replacements.reserve(persistent.size());
    for (const auto& previous : persistent) replacements.append(index(newRows.value(previous.row(), -1)));
    changePersistentIndexList(persistent, replacements);
    emit layoutChanged();
}
void ModListModel::setCheckable(const QSet<QString>& uuids, bool checked) {
    QList<int> rows;
    for (int row = 0; row < m_items.size(); ++row)
        if (uuids.contains(m_items[row].uuid) && m_items[row].checked != checked) rows.append(row);
    setCheckStates(rows, checked);
}
void ModListModel::setCheckStates(const QList<int>& rows, bool checked) {
    int first = static_cast<int>(m_items.size());
    for (int row : rows) if (getItem(row) && m_items[row].checked != checked) {
        m_items[row].checked = checked; first = std::min(first, row);
    }
    if (first == m_items.size()) return;
    const auto loadChanged = updateLoadIndices();
    if (!loadChanged.isEmpty()) first = std::min(first, loadChanged.first());
    emit dataChanged(index(first), index(rowCount()-1), {kCheckStateRole,kLoadIndexRole,kIsActiveRole,kSectionNameRole});
    emit activeModsChanged();
}
void ModListModel::setStartupImpact(const QHash<QString, double>& impacts) {
    if (m_items.isEmpty()) return;
    for (auto& item : m_items) {
        const auto* about = dynamic_cast<const AboutXmlMod*>(item.mod.get());
        item.startupImpactS = about ? impacts.value(about->packageId.str()) : 0;
    }
    emit dataChanged(index(0), index(rowCount()-1), {kStartupImpactRole,kStartupImpactColorRole,kStartupImpactTextRole});
}
void ModListModel::diagnosticsChanged(const QHash<QString, view::ModDiagnosticsView>& summaries) {
    if (m_items.isEmpty()) return;
    int first = rowCount(), last = -1;
    for (int row = 0; row < m_items.size(); ++row) {
        const auto& uuid = m_items[row].uuid;
        if (m_lastDiagnostics.contains(uuid) != summaries.contains(uuid) || m_lastDiagnostics.value(uuid) != summaries.value(uuid)) {
            first = std::min(first, row); last = row;
        }
    }
    m_lastDiagnostics = summaries;
    if (last >= 0) emit dataChanged(index(first), index(last), {kHasErrorRole,kHasWarningRole,kErrorTooltipRole,kWarningTooltipRole});
}
}
