#pragma once
#include <QAbstractListModel>
#include <optional>
#include "ui/models/mod_list_model.hpp"
namespace px::modrim {
class ModListProxyModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool isFiltered READ is_filtered NOTIFY is_filtered_changed)
    Q_PROPERTY(int activeCount READ active_count NOTIFY active_count_changed)
public:
    explicit ModListProxyModel(ModListModel* source, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int,QByteArray> roleNames() const override;
    QModelIndex mapToSource(const QModelIndex& index) const;
    QModelIndex mapFromSource(const QModelIndex& index) const;
    int proxy_row_for_uuid(const QString& uuid) const;
    void set_search_filter(const QString& text);
    void set_sidebar_filter(std::optional<QSet<QString>> uuids);
    bool is_filtered() const { return !search_text_.isEmpty() || sidebar_uuids_.has_value(); }
    int active_count() const { return visible_active_count_; }
    bool move_row(int source, int target, bool clamp_to_active = false);
signals:
    void is_filtered_changed();
    void active_count_changed();
private:
    void rebuild();
    void rebuild_source_mapping();
    void layout_rebuild();
    void update_visible_counts();
    bool accept_item(const ModItem& item) const;
    void source_data_changed(const QModelIndex& top, const QModelIndex& bottom, const QList<int>& roles);
    ModListModel* source_;
    QString search_text_;
    std::optional<QSet<QString>> sidebar_uuids_;
    QStringList visible_uuids_;
    QHash<QString,int> source_rows_, proxy_rows_;
    bool in_proxy_move_ = false;
    int visible_active_count_ = 0;
    int visible_inactive_count_ = 0;
};
}
