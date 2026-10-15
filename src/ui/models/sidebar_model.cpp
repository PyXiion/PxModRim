#include "ui/models/sidebar_model.h"
#include "ui/theme/palette.h"
namespace px::ui {
SidebarModel::SidebarModel(QObject* parent) : QAbstractListModel(parent) {}
int SidebarModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(items_.size()); }
QVariant SidebarModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= items_.size()) return {};
    const auto& item = items_[index.row()];
    switch (role) {
    case Qt::DisplayRole: case LabelRole: return item.entry.label;
    case CountRole: return item.entry.count();
    case BadgeBgRole: return item.badge_bg;
    case BadgeFgRole: return item.badge_fg;
    case IconRole: return item.icon_name;
    case IconColorRole: return item.icon_color;
    case SectionRole: return item.section_name;
    default: return {};
    }
}
QHash<int,QByteArray> SidebarModel::roleNames() const {
    return {{LabelRole,"filterLabel"},{CountRole,"count"},{BadgeBgRole,"badgeBg"},{BadgeFgRole,"badgeFg"},
        {IconRole,"iconName"},{IconColorRole,"iconColor"},{SectionRole,"sectionName"}};
}
SidebarItem SidebarModel::make_item(const core::view::SidebarEntry& entry) {
    using Kind = core::view::SidebarEntry::Kind;
    SidebarItem item{entry,paletteValue(u"ELEVATE_4"),paletteValue(u"TEXT_MUTED"),QStringLiteral("grid"),paletteValue(u"TEXT_DIM"),QStringLiteral("Status")};
    switch (entry.kind) {
    case Kind::All: break;
    case Kind::Active: item.icon_name = QStringLiteral("check-circle"); break;
    case Kind::Inactive: item.icon_name = QStringLiteral("ban"); break;
    case Kind::WithErrors:
        item.badge_bg = paletteValue(u"DANGER_BG_QML"); item.badge_fg = item.icon_color = paletteValue(u"DANGER"); item.icon_name = QStringLiteral("error"); break;
    case Kind::WithWarnings:
        item.badge_bg = paletteValue(u"WARNING_BG_QML"); item.badge_fg = item.icon_color = paletteValue(u"WARNING"); item.icon_name = QStringLiteral("warning"); break;
    case Kind::Provider: {
        item.section_name = QStringLiteral("Providers");
        item.icon_name = QStringLiteral("folder");
        const auto label = entry.label.toLower();
        if (label.contains("steam") || label.contains("downloaded")) item.icon_name = QStringLiteral("steam");
        else if (label.contains("local")) item.icon_name = QStringLiteral("folder");
        else if (label.contains("core")) item.icon_name = QStringLiteral("grid");
        else if (label.contains("git")) item.icon_name = QStringLiteral("git");
        break;
    }
    }
    return item;
}
void SidebarModel::set_entries(const QList<core::view::SidebarEntry>& entries) {
    beginResetModel(); items_.clear(); items_.reserve(entries.size());
    for (const auto& entry : entries) items_.append(make_item(entry));
    endResetModel();
}
void SidebarModel::update_entries(const QList<core::view::SidebarEntry>& entries) {
    if (entries.size() != items_.size()) { set_entries(entries); return; }
    QSet<int> changed;
    for (int row = 0; row < entries.size(); ++row) {
        auto next = make_item(entries[row]);
        const auto& old = items_[row];
        if (old.entry.label != next.entry.label) changed.insert(LabelRole);
        if (old.entry.count() != next.entry.count()) changed.insert(CountRole);
        if (old.badge_bg != next.badge_bg) changed.insert(BadgeBgRole);
        if (old.badge_fg != next.badge_fg) changed.insert(BadgeFgRole);
        if (old.icon_name != next.icon_name) changed.insert(IconRole);
        if (old.icon_color != next.icon_color) changed.insert(IconColorRole);
        if (old.section_name != next.section_name) changed.insert(SectionRole);
        items_[row] = std::move(next);
    }
    if (!changed.isEmpty() && !items_.isEmpty()) emit dataChanged(index(0), index(rowCount()-1), changed.values());
}
std::optional<core::view::SidebarEntry> SidebarModel::get_entry(int row) const {
    if (row < 0 || row >= items_.size()) return std::nullopt;
    return items_[row].entry;
}
}
