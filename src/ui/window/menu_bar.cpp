#include "ui/window/menu_bar.hpp"

#include <QMenu>

namespace px::modrim {

const std::array<MenuSpec, 4>& menuLayout() {
  static const std::array<MenuSpec, 4> layout{{
      {"&File",
       {ActionId::kSave, ActionId::kRestore, ActionId::kSettings, std::nullopt,
        ActionId::kQuit}},
      {"&Mods",
       {ActionId::kRefresh, ActionId::kFullRescan, ActionId::kAutoSort,
        std::nullopt, ActionId::kUpdateMods}},
      {"&View",
       {ActionId::kFocusSearch, ActionId::kNextView, ActionId::kPrevView,
        std::nullopt, ActionId::kFullscreen}},
      {"&Help",
       {ActionId::kReportIssue, ActionId::kUploadLogs, ActionId::kOpenLogs,
        ActionId::kShortcuts, ActionId::kCheckUpdates, std::nullopt,
        ActionId::kAbout}},
  }};
  return layout;
}

MenuBar::MenuBar(const ActionMap& actions, QWidget* parent) : QMenuBar(parent) {
  for (const auto& spec : menuLayout()) {
    auto* menu = addMenu(spec.title);
    for (auto id : spec.actions) {
      if (id)
        menu->addAction(actions.at(*id));
      else
        menu->addSeparator();
    }
  }
}

}  // namespace px::modrim
