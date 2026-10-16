#pragma once
#include "ui/views/base.h"
#include "ui/panels/sidebar_panel.h"
#include "ui/panels/mod_list_panel.h"
#include "ui/panels/mod_info_panel.h"
namespace px::modrim {
class ModsViewPanel : public BaseViewPanel {
    Q_OBJECT
public:
    static constexpr RailViewMeta kRailMeta{QLatin1StringView("mods"), QLatin1StringView("mods"), QLatin1StringView("Mods")};
    ModsViewPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app);
    void apply_sidebar_entry(const view::SidebarEntry& entry);
    void apply_current_sidebar_filter();
    SidebarPanel* sidebar;
    ModListPanel* mod_list;
    ModInfoPanel* mod_info;
signals:
    void entry_selected(const px::modrim::view::SidebarEntry& entry);
    void mod_selected(const QString& uuid);
    void active_mods_changed();
    void order_changed();
};
}
