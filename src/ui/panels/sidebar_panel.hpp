#pragma once
#include "ui/components/filter_sidebar.hpp"
#include "ui/models/sidebar_model.hpp"
#include <cstdint>
namespace px::modrim { class CoreContext; }
namespace px::modrim {
class SidebarPanel : public FilterSidebar {
    Q_OBJECT
public:
    SidebarPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent = nullptr);
    ~SidebarPanel() override;
    void set_entries(const QList<view::SidebarEntry>& entries);
    std::optional<view::SidebarEntry> current_entry() const;
    void select_all_entry();
    SidebarModel* model() const { return model_; }
signals:
    void entry_selected(const px::modrim::view::SidebarEntry& entry);
private:
    void on_activated(int row);
    CoreContext& ctx_;
    SidebarModel* model_;
    QList<view::SidebarEntry> entries_;
    std::uint64_t sidebar_connection_;
};
}
