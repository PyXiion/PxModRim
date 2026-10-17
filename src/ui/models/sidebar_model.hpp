#pragma once
#include <QAbstractListModel>
#include <optional>

#include "core/models/view/sidebar.hpp"
namespace px::modrim {
struct SidebarItem {
  view::SidebarEntry entry;
  QString badgeBg, badgeFg, iconName, iconColor, sectionName;
};
class SidebarModel : public QAbstractListModel {
  Q_OBJECT
 public:
  enum Role {
    kLabelRole = Qt::UserRole + 1,
    kCountRole,
    kBadgeBgRole,
    kBadgeFgRole,
    kIconRole,
    kIconColorRole,
    kSectionRole
  };
  Q_ENUM(Role)
  explicit SidebarModel(QObject* parent = nullptr);
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;
  void setEntries(const QList<view::SidebarEntry>& entries);
  void updateEntries(const QList<view::SidebarEntry>& entries);
  std::optional<view::SidebarEntry> getEntry(int row) const;

 private:
  static SidebarItem makeItem(const view::SidebarEntry& entry);
  QList<SidebarItem> m_items;
};
}  // namespace px::modrim
