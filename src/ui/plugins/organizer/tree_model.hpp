#pragma once

#include <QAbstractItemModel>
#include <QVariantList>
#include <functional>
#include <memory>
#include <vector>

#include "core/models/view/diagnostics.hpp"
#include "core/organizer/resolve.hpp"

namespace px::modrim {

inline const QString kUngroupedKey = QStringLiteral("ungrouped");

QString providerIcon(const QString& id);

struct TreeNode {
  QString kind = QStringLiteral("root");
  QString key;
  TreeNode* parent = nullptr;
  const organizer::FolderNode* folder = nullptr;
  const organizer::ModLeaf* leaf = nullptr;
  QString providerId;
  QString providerLabel;
  QString modVersion;
  int row = 0;
  std::vector<std::unique_ptr<TreeNode>> children;

  void add(std::unique_ptr<TreeNode> child);
  QList<TreeNode*> walk() const;
  qint64 folderId() const;
  bool isContainer() const;
};

class ModTreeModel : public QAbstractItemModel {
  Q_OBJECT
 public:
  enum Role {
    kKindRole = Qt::UserRole + 1,
    kKeyRole,
    kUuidRole,
    kPackageIdRole,
    kCheckStateRole,
    kEnabledCountRole,
    kTotalCountRole,
    kVisibleCountRole,
    kFilteringRole,
    kHasRuleRole,
    kProviderIconRole,
    kProviderColorRole,
    kHasErrorRole,
    kErrorTooltipRole,
    kCanPlaceRole,
    kSelectedRole,
    kHasWarningRole,
    kWarningTooltipRole,
    kTagsRole,
    kProviderLabelRole,
    kModVersionRole
  };
  using SummaryFor =
      std::function<std::optional<view::ModDiagnosticsView>(const QString&)>;
  explicit ModTreeModel(SummaryFor summaryFor, QObject* parent = nullptr);
  QModelIndex index(int row, int column,
                    const QModelIndex& parent = {}) const override;
  QModelIndex parent(const QModelIndex& child) const override;
  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  bool filtering() const;
  void setTree(organizer::FolderNode root, const organizer::ModsByUuid& mods,
               const QHash<QString, QString>& providerColors, bool filtering,
               const QHash<qint64, organizer::Tag>& tags = {});
  void refreshDiagnostics();
  TreeNode* nodeAt(const QModelIndex& index) const;
  Q_INVOKABLE QModelIndex indexForKey(const QString& key) const;
  QModelIndex indexForUuid(const QString& uuid) const;
  QList<QModelIndex> expandedIndexes(const QModelIndex& parent = {}) const;
  QList<TreeNode*> selectedNodes() const;
  bool isSelected(const QModelIndex& index) const;
  void select(const QList<QModelIndex>& indexes,
              const QString& mode = QStringLiteral("replace"));

 private:
  TreeNode* node(const QModelIndex& index) const;
  QModelIndex indexOf(const TreeNode* node) const;
  void emitAllChanged(TreeNode* node, const QList<int>& roles = {});
  void notifySelection(const QSet<QString>& changed);
  SummaryFor m_summaryFor;
  std::unique_ptr<TreeNode> m_root;
  std::unique_ptr<organizer::FolderNode> m_payload;
  bool m_filtering = false;
  QHash<QString, QString> m_providerColors;
  QHash<QString, QVariantList> m_tagRows;
  QSet<QString> m_selected;
};

}  // namespace px::modrim
