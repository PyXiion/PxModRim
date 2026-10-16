#include "ui/models/sidebar_model.hpp"

#include "ui/theme/palette.hpp"
namespace px::modrim {
SidebarModel::SidebarModel(QObject* parent) : QAbstractListModel(parent) {}
int SidebarModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}
QVariant SidebarModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
    return {};
  const auto& item = m_items[index.row()];
  switch (role) {
    case Qt::DisplayRole:
    case kLabelRole:
      return item.entry.label;
    case kCountRole:
      return item.entry.count();
    case kBadgeBgRole:
      return item.badgeBg;
    case kBadgeFgRole:
      return item.badgeFg;
    case kIconRole:
      return item.iconName;
    case kIconColorRole:
      return item.iconColor;
    case kSectionRole:
      return item.sectionName;
    default:
      return {};
  }
}
QHash<int, QByteArray> SidebarModel::roleNames() const {
  return {{kLabelRole, "filterLabel"},  {kCountRole, "count"},
          {kBadgeBgRole, "badgeBg"},    {kBadgeFgRole, "badgeFg"},
          {kIconRole, "iconName"},      {kIconColorRole, "iconColor"},
          {kSectionRole, "sectionName"}};
}
SidebarItem SidebarModel::makeItem(const view::SidebarEntry& entry) {
  using Kind = view::SidebarEntry::Kind;
  SidebarItem item{entry,
                   paletteValue(u"ELEVATE_4"),
                   paletteValue(u"TEXT_MUTED"),
                   QStringLiteral("grid"),
                   paletteValue(u"TEXT_DIM"),
                   QStringLiteral("Status")};
  switch (entry.kind) {
    case Kind::kAll:
      break;
    case Kind::kActive:
      item.iconName = QStringLiteral("check-circle");
      break;
    case Kind::kInactive:
      item.iconName = QStringLiteral("ban");
      break;
    case Kind::kWithErrors:
      item.badgeBg = paletteValue(u"DANGER_BG_QML");
      item.badgeFg = item.iconColor = paletteValue(u"DANGER");
      item.iconName = QStringLiteral("error");
      break;
    case Kind::kWithWarnings:
      item.badgeBg = paletteValue(u"WARNING_BG_QML");
      item.badgeFg = item.iconColor = paletteValue(u"WARNING");
      item.iconName = QStringLiteral("warning");
      break;
    case Kind::kProvider: {
      item.sectionName = QStringLiteral("Providers");
      item.iconName = QStringLiteral("folder");
      const auto label = entry.label.toLower();
      if (label.contains("steam") || label.contains("downloaded"))
        item.iconName = QStringLiteral("steam");
      else if (label.contains("local"))
        item.iconName = QStringLiteral("folder");
      else if (label.contains("core"))
        item.iconName = QStringLiteral("grid");
      else if (label.contains("git"))
        item.iconName = QStringLiteral("git");
      break;
    }
  }
  return item;
}
void SidebarModel::setEntries(const QList<view::SidebarEntry>& entries) {
  beginResetModel();
  m_items.clear();
  m_items.reserve(entries.size());
  for (const auto& entry : entries) m_items.append(makeItem(entry));
  endResetModel();
}
void SidebarModel::updateEntries(const QList<view::SidebarEntry>& entries) {
  if (entries.size() != m_items.size()) {
    setEntries(entries);
    return;
  }
  QSet<int> changed;
  for (int row = 0; row < entries.size(); ++row) {
    auto next = makeItem(entries[row]);
    const auto& old = m_items[row];
    if (old.entry.label != next.entry.label) changed.insert(kLabelRole);
    if (old.entry.count() != next.entry.count()) changed.insert(kCountRole);
    if (old.badgeBg != next.badgeBg) changed.insert(kBadgeBgRole);
    if (old.badgeFg != next.badgeFg) changed.insert(kBadgeFgRole);
    if (old.iconName != next.iconName) changed.insert(kIconRole);
    if (old.iconColor != next.iconColor) changed.insert(kIconColorRole);
    if (old.sectionName != next.sectionName) changed.insert(kSectionRole);
    m_items[row] = std::move(next);
  }
  if (!changed.isEmpty() && !m_items.isEmpty())
    emit dataChanged(index(0), index(rowCount() - 1), changed.values());
}
std::optional<view::SidebarEntry> SidebarModel::getEntry(int row) const {
  if (row < 0 || row >= m_items.size()) return std::nullopt;
  return m_items[row].entry;
}
}  // namespace px::modrim
