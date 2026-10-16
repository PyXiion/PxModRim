#pragma once
#include "ui/views/base.hpp"
#include "ui/panels/sidebar_panel.hpp"
#include "ui/panels/mod_list_panel.hpp"
#include "ui/panels/mod_info_panel.hpp"
namespace px::modrim {
class ModsViewPanel : public BaseViewPanel {
    Q_OBJECT
public:
    static constexpr RailViewMeta kRailMeta{QLatin1StringView("mods"), QLatin1StringView("mods"), QLatin1StringView("Mods")};
    ModsViewPanel(CoreContext& ctx, QQmlEngine* engine, QWidget* parent, AppContext& app);
    void applySidebarEntry(const view::SidebarEntry& entry);
    void applyCurrentSidebarFilter();
    SidebarPanel* sidebar;
    ModListPanel* modList;
    ModInfoPanel* modInfo;
signals:
    void entrySelected(const px::modrim::view::SidebarEntry& entry);
    void modSelected(const QString& uuid);
    void activeModsChanged();
    void orderChanged();
};
}
