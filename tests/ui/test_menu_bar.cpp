#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <set>

#include <QAction>
#include <QApplication>
#include <QKeySequence>
#include <QMainWindow>
#include <QMenu>

#include "ui/window/actions.h"
#include "ui/window/menu_bar.h"

namespace {
void ensureApplication() {
    static int argc = 1;
    static char name[] = "pxmodrim-menu-tests";
    static char* argv[] = {name, nullptr};
    static std::unique_ptr<QApplication> app;
    if (!QApplication::instance()) app = std::make_unique<QApplication>(argc, argv);
}
}

TEST_CASE("Every window action belongs to exactly one menu", "[ui][window]") {
    std::multiset<px::ui::ActionId> placed;
    for (const auto& menu : px::ui::menuLayout())
        for (const auto id : menu.actions) if (id) placed.insert(*id);
    REQUIRE(placed.size() == px::ui::actionSpecs().size());
    for (const auto& spec : px::ui::actionSpecs()) CHECK(placed.count(spec.id) == 1);
}

TEST_CASE("Window shortcuts are unique and view switching is collapsed", "[ui][window]") {
    ensureApplication();
    std::set<QString> shortcuts;
    for (const auto& spec : px::ui::actionSpecs())
        if (!spec.shortcut.isEmpty()) CHECK(shortcuts.insert(spec.shortcut).second);
    std::set<std::pair<QString, QString>> unique;
    int viewRows = 0;
    bool save = false, refresh = false;
    for (const auto& row : px::ui::shortcutRows()) {
        CHECK(unique.emplace(row.first, row.second).second);
        if (row.second == QStringLiteral("Switch to view")) ++viewRows;
        save |= row.second == QStringLiteral("Save mod list");
        refresh |= row.second == QStringLiteral("Refresh mods");
    }
    CHECK(viewRows == 1);
    CHECK(save);
    CHECK(refresh);
}

TEST_CASE("Hidden menu actions retain their window shortcuts", "[ui][window]") {
    ensureApplication();
    QMainWindow window;
    const auto actions = px::ui::createActions(window);
    px::ui::MenuBar menu(actions, &window);
    menu.hide();
    bool triggered = false;
    QObject::connect(actions.at(px::ui::ActionId::Save), &QAction::triggered, &window, [&] { triggered = true; });
    QMenu* file = nullptr;
    for (auto* item : menu.findChildren<QMenu*>()) if (item->title() == QStringLiteral("&File")) file = item;
    REQUIRE(file != nullptr);
    auto* save = actions.at(px::ui::ActionId::Save);
    CHECK(file->actions().contains(save));
    CHECK(save->shortcut() == QKeySequence(QStringLiteral("Ctrl+S")));
    CHECK(window.actions().contains(save));
    save->trigger();
    CHECK(triggered);
}
