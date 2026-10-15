#pragma once
#include <QAbstractListModel>
#include <optional>
#include "core/models/view/sidebar.h"
namespace px::ui {
struct SidebarItem {
    core::view::SidebarEntry entry;
    QString badge_bg, badge_fg, icon_name, icon_color, section_name;
};
class SidebarModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { LabelRole = Qt::UserRole + 1, CountRole, BadgeBgRole, BadgeFgRole, IconRole, IconColorRole, SectionRole };
    Q_ENUM(Role)
    explicit SidebarModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int,QByteArray> roleNames() const override;
    void set_entries(const QList<core::view::SidebarEntry>& entries);
    void update_entries(const QList<core::view::SidebarEntry>& entries);
    std::optional<core::view::SidebarEntry> get_entry(int row) const;
private:
    static SidebarItem make_item(const core::view::SidebarEntry& entry);
    QList<SidebarItem> items_;
};
}
