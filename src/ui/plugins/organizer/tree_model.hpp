#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <QAbstractItemModel>
#include <QVariantList>

#include "core/models/view/diagnostics.hpp"
#include "core/organizer/resolve.hpp"

namespace px::modrim {

inline const QString UNGROUPED_KEY = QStringLiteral("ungrouped");

struct TreeNode {
    QString kind;
    QString key;
    TreeNode* parent = nullptr;
    const organizer::FolderNode* folder = nullptr;
    const organizer::ModLeaf* leaf = nullptr;
    QString provider_id;
    QString provider_label;
    QString mod_version;
    int row = 0;
    std::vector<std::unique_ptr<TreeNode>> children;

    void add(std::unique_ptr<TreeNode> child);
    QList<TreeNode*> walk() const;
    qint64 folder_id() const;
    TreeNode(QString kind = QStringLiteral("root"), QString key = {}, TreeNode* parent = nullptr,
             const organizer::FolderNode* folder = nullptr,
             const organizer::ModLeaf* leaf = nullptr, QString provider_id = {});
    bool is_container() const;
};

class ModTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Role {
        KindRole = Qt::UserRole + 1, KeyRole, UuidRole, PackageIdRole,
        CheckStateRole, EnabledCountRole, TotalCountRole, VisibleCountRole,
        FilteringRole, HasRuleRole, ProviderIconRole, ProviderColorRole,
        HasErrorRole, ErrorTooltipRole, CanPlaceRole, SelectedRole,
        HasWarningRole, WarningTooltipRole, TagsRole, ProviderLabelRole, ModVersionRole
    };
    Q_ENUM(Role)
    using SummaryFor = std::function<std::optional<view::ModDiagnosticsView>(const QString&)>;
    explicit ModTreeModel(SummaryFor summary_for, QObject* parent = nullptr);
    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool filtering() const;
    void set_tree(const organizer::FolderNode& root,
                  const organizer::ModsByUuid& mods,
                  const QHash<QString, QString>& provider_colors, bool filtering,
                  const QHash<qint64, organizer::Tag>& tags = {});
    void refresh_diagnostics();
    TreeNode* node_at(const QModelIndex& index) const;
    QModelIndex index_for_key(const QString& key) const;
    QModelIndex index_for_uuid(const QString& uuid) const;
    QList<QModelIndex> expanded_indexes(const QModelIndex& parent = {}) const;
    QList<TreeNode*> selected_nodes() const;
    bool is_selected(const QModelIndex& index) const;
    void select(const QList<QModelIndex>& indexes, const QString& mode = QStringLiteral("replace"));
    void clear_selection();

private:
    TreeNode* node(const QModelIndex& index) const;
    QModelIndex index_of(const TreeNode* node) const;
    void emit_all_changed(TreeNode* node, const QList<int>& roles = {});
    void notify_selection(const QSet<QString>& changed);
    SummaryFor summary_for_;
    std::unique_ptr<TreeNode> root_;
    std::unique_ptr<organizer::FolderNode> payload_;
    bool filtering_ = false;
    QHash<QString, QString> provider_colors_;
    QHash<QString, QVariantList> tag_rows_;
    QSet<QString> selected_;
};

}  // namespace px::modrim
