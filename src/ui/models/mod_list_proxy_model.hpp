#pragma once
#include <QAbstractListModel>
#include <optional>

#include "ui/models/mod_list_model.hpp"
namespace px::modrim {
class ModListProxyModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(bool isFiltered READ isFiltered NOTIFY isFilteredChanged)
  Q_PROPERTY(int activeCount READ activeCount NOTIFY activeCountChanged)
 public:
  explicit ModListProxyModel(ModListModel* source, QObject* parent = nullptr);
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index,
                int role = Qt::DisplayRole) const override;
  bool setData(const QModelIndex& index, const QVariant& value,
               int role = Qt::EditRole) override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;
  QHash<int, QByteArray> roleNames() const override;
  QModelIndex mapToSource(const QModelIndex& index) const;
  QModelIndex mapFromSource(const QModelIndex& index) const;
  int proxyRowForUuid(const QString& uuid) const;
  void setSearchFilter(const QString& text);
  void setSidebarFilter(std::optional<QSet<QString>> uuids);
  bool isFiltered() const {
    return !m_searchText.isEmpty() || m_sidebarUuids.has_value();
  }
  int activeCount() const { return m_visibleActiveCount; }
  bool moveRow(int source, int target, bool clampToActive = false);
 signals:
  void isFilteredChanged();
  void activeCountChanged();

 private:
  void rebuild();
  void rebuildSourceMapping();
  void layoutRebuild();
  void updateVisibleCounts();
  bool acceptItem(const ModItem& item) const;
  void sourceDataChanged(const QModelIndex& top, const QModelIndex& bottom,
                         const QList<int>& roles);
  ModListModel* m_source;
  QString m_searchText;
  std::optional<QSet<QString>> m_sidebarUuids;
  QStringList m_visibleUuids;
  QHash<QString, int> m_sourceRows, m_proxyRows;
  bool m_inProxyMove = false;
  int m_visibleActiveCount = 0;
  int m_visibleInactiveCount = 0;
};
}  // namespace px::modrim
