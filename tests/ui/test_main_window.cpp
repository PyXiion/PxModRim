#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <memory>

#include <QCoro/QCoroTask>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QEventLoop>
#include <QMessageBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTimer>

#include "core/config.hpp"
#include "core/context.hpp"
#include "core/downloads/manager.hpp"
#include "core/mod_service.hpp"
#include "core/models/metadata/structures.hpp"
#include "ui/context.hpp"
#include "ui/window/main_window.hpp"

namespace {
void ensureApplication() {
    static int argc = 1;
    static char name[] = "pxmodrim-window-tests";
    static char* argv[] = {name, nullptr};
    static std::unique_ptr<QApplication> app;
    if (!QApplication::instance()) app = std::make_unique<QApplication>(argc, argv);
}

class FirstView : public px::modrim::RailView {
public:
    using RailView::RailView;
    static constexpr px::modrim::RailViewMeta kRailMeta{QLatin1StringView("first"), QLatin1StringView("mods"), QLatin1StringView("First")};
};
class SecondView : public px::modrim::RailView {
public:
    using RailView::RailView;
    static constexpr px::modrim::RailViewMeta kRailMeta{QLatin1StringView("second"), QLatin1StringView("mods"), QLatin1StringView("Second")};
    void openRoute(const QStringList& path) override { opened = path; }
    QStringList opened;
};
class ThirdView : public px::modrim::RailView {
public:
    using RailView::RailView;
    static constexpr px::modrim::RailViewMeta kRailMeta{QLatin1StringView("third"), QLatin1StringView("mods"), QLatin1StringView("Third")};
};

struct Fixture {
    QTemporaryDir directory;
    px::modrim::ConfigService configService{directory.path()};
    std::unique_ptr<px::modrim::CoreContext> core;
    std::unique_ptr<px::modrim::AppContext> app;
    explicit Fixture(bool canSave = true) {
        ensureApplication();
        px::modrim::AppConfig config;
        if (canSave) {
            config.paths.config_folder = QDir(directory.path()).filePath(QStringLiteral("game-config")).toStdString();
            QDir().mkpath(QString::fromStdString(config.paths.config_folder));
        }
        core = px::modrim::CoreContext::create(config, &configService);
        core->registerPlugin(std::make_unique<px::modrim::DownloadManager>());
        auto a = std::make_shared<px::modrim::AboutXmlMod>();
        a->package_id = px::modrim::CaseInsensitiveStr(u"test.a");
        auto b = std::make_shared<px::modrim::AboutXmlMod>();
        b->package_id = px::modrim::CaseInsensitiveStr(u"test.b");
        core->load({{QStringLiteral("a"), a}, {QStringLiteral("b"), b}}, {QStringLiteral("a"), QStringLiteral("b")});
        app = std::make_unique<px::modrim::AppContext>(*core);
        app->addRailView<FirstView>();
        app->addRailView<SecondView>();
        app->addRailView<ThirdView>();
    }
};

QStackedWidget* stackOf(px::modrim::MainWindow& window) {
    return window.findChild<QStackedWidget*>(QStringLiteral("viewStack"));
}

QMessageBox* closePrompt(px::modrim::MainWindow& window) {
    for (auto* dialog : window.findChildren<QMessageBox*>())
        if (dialog->windowTitle() == QStringLiteral("Unsaved Changes") && dialog->isVisible()) return dialog;
    return nullptr;
}
}

TEST_CASE("View shortcuts wrap and ignore invalid indexes", "[ui][window]") {
    Fixture fixture;
    px::modrim::MainWindow window(*fixture.app);
    auto* stack = stackOf(window);
    REQUIRE(stack != nullptr);
    window.cycleView(-1);
    CHECK(stack->currentIndex() == 2);
    window.cycleView(1);
    CHECK(stack->currentIndex() == 0);
    window.selectView(3);
    window.selectView(-1);
    CHECK(stack->currentIndex() == 0);
}

TEST_CASE("Routes select their view before handing it the path", "[ui][window]") {
    Fixture fixture;
    px::modrim::MainWindow window(*fixture.app);
    auto* stack = stackOf(window);
    REQUIRE(stack != nullptr);
    window.openRoute(QStringLiteral("modrim://second/mod/42"));
    CHECK(stack->currentIndex() == 1);
    CHECK(static_cast<SecondView*>(stack->widget(1))->opened == QStringList{"mod", "42"});
    window.openRoute(QStringLiteral("modrim://missing/x"));
    window.openRoute(QStringLiteral("second"));
    CHECK(stack->currentIndex() == 1);
}

TEST_CASE("Settings route leaves the selected view unchanged", "[ui][window]") {
    Fixture fixture;
    px::modrim::MainWindow window(*fixture.app);
    window.selectView(1);
    window.openRoute(QStringLiteral("modrim://settings"));
    CHECK(stackOf(window)->currentIndex() == 1);
    bool found = false;
    for (auto* dialog : window.findChildren<QDialog*>()) {
        if (dialog->isVisible()) { found = true; dialog->reject(); }
    }
    CHECK(found);
}

TEST_CASE("Dirty state tracks active list order and disk reload baseline", "[ui][window]") {
    Fixture fixture;
    px::modrim::MainWindow window(*fixture.app);
    CHECK_FALSE(window.hasUnsavedChanges());
    fixture.core->setActive({QStringLiteral("b"), QStringLiteral("a")});
    CHECK(window.hasUnsavedChanges());
    CHECK(window.isWindowModified());
    fixture.core->setActive({QStringLiteral("a"), QStringLiteral("b")});
    CHECK_FALSE(window.hasUnsavedChanges());
    fixture.core->setActive({QStringLiteral("a")});
    fixture.core->modService().mods_changed.notify(std::monostate{});
    CHECK(window.hasUnsavedChanges());
}

TEST_CASE("Successful saves clear the unsaved state", "[ui][window]") {
    Fixture fixture;
    px::modrim::MainWindow window(*fixture.app);
    fixture.core->setActive({QStringLiteral("b"), QStringLiteral("a")});
    REQUIRE(window.hasUnsavedChanges());
    CHECK(QCoro::waitFor(window.saveActiveMods()));
    CHECK_FALSE(window.hasUnsavedChanges());
    CHECK_FALSE(window.isWindowModified());
}

TEST_CASE("Dirty close prompts once and honors Save Discard and Cancel", "[ui][window]") {
    const auto choice = GENERATE(QMessageBox::Save, QMessageBox::Discard, QMessageBox::Cancel);
    Fixture fixture;
    px::modrim::MainWindow window(*fixture.app);
    fixture.core->setActive({QStringLiteral("a")});
    int closed = 0;
    QEventLoop loop;
    window.setAppQuitCallback([&] { ++closed; loop.quit(); });
    QCloseEvent first, second;
    QCoreApplication::sendEvent(&window, &first);
    QCoreApplication::sendEvent(&window, &second);
    CHECK_FALSE(first.isAccepted());
    CHECK_FALSE(second.isAccepted());
    auto* prompt = closePrompt(window);
    REQUIRE(prompt != nullptr);
    int visiblePrompts = 0;
    for (auto* item : window.findChildren<QMessageBox*>()) if (item->isVisible()) ++visiblePrompts;
    CHECK(visiblePrompts == 1);
    QTimer::singleShot(10, prompt, [prompt, choice] { prompt->done(choice); });
    QTimer::singleShot(choice == QMessageBox::Cancel ? 40 : 3000, &loop, &QEventLoop::quit);
    loop.exec();
    CHECK(closed == (choice == QMessageBox::Cancel ? 0 : 1));
}

TEST_CASE("A failed close-time save leaves the window open", "[ui][window]") {
    Fixture fixture(false);
    px::modrim::MainWindow window(*fixture.app);
    fixture.core->setActive({QStringLiteral("a")});
    bool closed = false;
    window.setAppQuitCallback([&] { closed = true; });
    QCloseEvent event;
    QCoreApplication::sendEvent(&window, &event);
    auto* prompt = closePrompt(window);
    REQUIRE(prompt != nullptr);
    prompt->done(QMessageBox::Save);
    CHECK_FALSE(closed);
    CHECK(window.hasUnsavedChanges());
}
