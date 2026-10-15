#pragma once
#include "ui/components/filter_sidebar.h"
#include "ui/models/sidebar_model.h"
#include <cstdint>
namespace px::core { class CoreContext; }
namespace px::ui {
class SidebarPanel : public FilterSidebar {
    Q_OBJECT
public:
    SidebarPanel(core::CoreContext& ctx, QQmlEngine* engine, QWidget* parent = nullptr);
    ~SidebarPanel() override;
    void set_entries(const QList<core::view::SidebarEntry>& entries);
    std::optional<core::view::SidebarEntry> current_entry() const;
    void select_all_entry();
    SidebarModel* model() const { return model_; }
signals:
    void entry_selected(const px::core::view::SidebarEntry& entry);
private:
    void on_activated(int row);
    core::CoreContext& ctx_;
    SidebarModel* model_;
    QList<core::view::SidebarEntry> entries_;
    std::uint64_t sidebar_connection_;
};
}
