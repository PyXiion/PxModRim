#include <catch2/catch_test_macros.hpp>

#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QString>
#include <QWidget>

#include "test_support.hpp"
#include "core/context.hpp"
#include "ui/components/svg_provider.hpp"
#include "ui/context.hpp"
#include "ui/theme/qml_theme.hpp"

#ifdef PX_HAS_STEAM
#include "ui/plugins/steam_workshop/view.hpp"

namespace {
// The embedded browser exists only in WebEngine builds; without it there is no "workshopWeb" object.
QObject* webOf(const px::modrim::SteamWorkshopViewPanel& view) {
    for (auto* quick : view.findChildren<QQuickWidget*>()) {
        if (auto* root = quick->rootObject()) {
            if (auto* web = root->findChild<QObject*>(QStringLiteral("workshopWeb"))) return web;
        }
    }
    return nullptr;
}
}  // namespace

TEST_CASE("steam workshop view preloads without error") {
    QWidget host;
    auto* engine = px::modrim::createQmlEngine(&host);
    engine->rootContext()->setContextProperty(QStringLiteral("Theme"), new px::modrim::Theme(engine));
    auto core = px::ui_test::makeCore();
    px::modrim::AppContext app(*core);
    px::modrim::SteamWorkshopViewPanel view(*core, engine, &host, app);
    view.preload();
}

TEST_CASE("steam workshop view has no web view before initialization") {
    QWidget host;
    auto* engine = px::modrim::createQmlEngine(&host);
    engine->rootContext()->setContextProperty(QStringLiteral("Theme"), new px::modrim::Theme(engine));
    auto core = px::ui_test::makeCore();
    px::modrim::AppContext app(*core);
    px::modrim::SteamWorkshopViewPanel view(*core, engine, &host, app);
    CHECK(webOf(view) == nullptr);
}
#endif
