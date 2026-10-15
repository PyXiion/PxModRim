#include "ui/window/menu_bar.h"

#include <QMenu>

namespace px::ui {

const std::array<MenuSpec, 4>& menuLayout() {
    static const std::array<MenuSpec, 4> layout{{
        {"&File", {ActionId::Save, ActionId::Restore, ActionId::Settings, std::nullopt, ActionId::Quit}},
        {"&Mods", {ActionId::Refresh, ActionId::FullRescan, ActionId::AutoSort, std::nullopt, ActionId::UpdateMods}},
        {"&View", {ActionId::FocusSearch, ActionId::NextView, ActionId::PrevView, std::nullopt, ActionId::Fullscreen}},
        {"&Help", {ActionId::ReportIssue, ActionId::UploadLogs, ActionId::OpenLogs, ActionId::Shortcuts,
                   ActionId::CheckUpdates, std::nullopt, ActionId::About}},
    }};
    return layout;
}

MenuBar::MenuBar(const ActionMap& actions, QWidget* parent) : QMenuBar(parent) {
    for (const auto& spec : menuLayout()) {
        auto* menu = addMenu(spec.title);
        for (auto id : spec.actions) {
            if (id) menu->addAction(actions.at(*id));
            else menu->addSeparator();
        }
    }
}

}  // namespace px::ui
