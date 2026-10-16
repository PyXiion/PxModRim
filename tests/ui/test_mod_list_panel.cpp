#include <catch2/catch_test_macros.hpp>
#include <QAbstractButton>
#include <QColor>
#include <QJSValue>
#include <QLineEdit>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWidget>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <memory>
#include "test_support.hpp"
#include "core/context.hpp"
#include "core/services/diagnostics_service.hpp"
#include "ui/components/mod_activation.hpp"
#include "ui/components/svg_provider.hpp"
#include "ui/panels/mod_list_panel.hpp"
#include "ui/theme/qml_theme.hpp"
#include "../core/services/services_support.hpp"
using px::modrim::ModListPanel;
namespace {
struct PanelFixture {
    std::unique_ptr<px::modrim::CoreContext> ctx;
    QWidget parent;
    QQmlEngine* engine;
    std::unique_ptr<ModListPanel> panel;
    PanelFixture(bool active = true, bool dependencies = false) {
        ctx = px::ui_test::makeCore();
        QHash<QString,px::modrim::ListedModPtr> mods;
        for (int i = 0; i < 3; ++i) {
            const auto letter = QChar('a'+i);
            auto mod = std::make_shared<px::modrim::AboutXmlMod>();
            mod->name = QStringLiteral("Mod %1").arg(QChar('A'+i));
            mod->packageId = px::modrim::CaseInsensitiveStr(QStringLiteral("mod.%1").arg(letter));
            mod->providerId = QStringLiteral("stub");
            if (dependencies && i > 0) {
                px::modrim::DependencyMod dependency;
                dependency.packageId = px::modrim::CaseInsensitiveStr(QStringLiteral("mod.%1").arg(QChar('a'+i-1)));
                mod->aboutRules.dependencies.append(dependency);
            }
            mods.insert(QStringLiteral("uuid-%1").arg(letter),mod);
        }
        ctx->load(mods,active ? QStringList{"uuid-a","uuid-b","uuid-c"} : QStringList{});
        ctx->diagnosticsService().rebuild();
        engine = px::modrim::createQmlEngine(&parent);
        engine->rootContext()->setContextProperty(QStringLiteral("Theme"),new px::modrim::Theme(engine));
        panel = std::make_unique<ModListPanel>(*ctx,engine,&parent);
        panel->loadMods(mods,ctx->activeUuids());
    }
    QObject* root() const { return panel->qmlWidget()->rootObject(); }
    QObject* view() const { return root() ? root()->findChild<QObject*>(QStringLiteral("listView")) : nullptr; }
    void select(int row, const QString& uuid) const {
        REQUIRE(root());
        REQUIRE(QMetaObject::invokeMethod(root(),"selectRow",Qt::DirectConnection,
            Q_ARG(QVariant,QVariant(row)),Q_ARG(QVariant,QVariant(uuid)),Q_ARG(QVariant,QVariant(0))));
    }
    void show() const {
        panel->resize(420,320); panel->show(); QTest::qWait(20);
        REQUIRE(panel->qmlWidget()->status() == QQuickWidget::Ready);
        REQUIRE(root()); REQUIRE(view());
    }
};
QVariant qmlValue(QObject* object,const char* name) {
    const auto value = object->property(name);
    return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
}
void toggleWithAnswer(PanelFixture& f, const QList<int>& rows, QMessageBox::StandardButton answer, QString* prompt = nullptr) {
    QTimer click(&f.parent); click.setInterval(1);
    QObject::connect(&click,&QTimer::timeout,&f.parent,[&] {
        for (auto* box : f.panel->findChildren<QMessageBox*>()) if (box->isVisible()) {
            if (prompt) *prompt = box->text();
            click.stop(); box->button(answer)->click(); return;
        }
    });
    click.start(); services_test::runAsync(f.panel->toggleRows(rows));
}
}
TEST_CASE("disabling dependencies supports selected only all dependents and cancellation") {
    SECTION("selected only") {
        PanelFixture f(true,true); QString prompt;
        toggleWithAnswer(f,{0},QMessageBox::No,&prompt);
        CHECK(f.panel->activeUuids() == QStringList{"uuid-b","uuid-c"});
        CHECK(prompt.contains("Mod B (mod.b)")); CHECK(prompt.contains("Mod C (mod.c)"));
    }
    SECTION("selected plus transitive dependents") {
        PanelFixture f(true,true);
        toggleWithAnswer(f,{0,1},QMessageBox::Yes);
        CHECK(f.panel->activeUuids().isEmpty());
    }
    SECTION("cancel") {
        PanelFixture f(true,true);
        toggleWithAnswer(f,{0},QMessageBox::Cancel);
        CHECK(f.panel->activeUuids() == QStringList{"uuid-a","uuid-b","uuid-c"});
    }
}
TEST_CASE("disabled mods leave the active block and reenabling restores dependency position") {
    PanelFixture f(true,true);
    toggleWithAnswer(f,{1},QMessageBox::No);
    CHECK(f.panel->activeUuids() == QStringList{"uuid-a","uuid-c"});
    const auto* inactive = f.panel->model()->getItem(2);
    REQUIRE(inactive); CHECK(inactive->uuid == "uuid-b"); CHECK_FALSE(inactive->checked); CHECK(inactive->loadIndex == -1);
    services_test::runAsync(f.panel->toggleRows({f.panel->rowForUuid("uuid-b")}));
    CHECK(f.panel->activeUuids() == QStringList{"uuid-a","uuid-b","uuid-c"});
    CHECK(f.ctx->activeUuids() == f.panel->activeUuids());
}
TEST_CASE("hover and tiny drag handle jitter never display the drag proxy") {
    PanelFixture f; f.show();
    auto* dragProxy = f.root()->findChild<QObject*>(QStringLiteral("dragProxy")); REQUIRE(dragProxy);
    auto* widget = f.panel->qmlWidget();
    QTest::mouseMove(widget,QPoint(4,26)); QTest::mouseMove(widget,QPoint(14,26));
    CHECK_FALSE(dragProxy->property("visible").toBool());
    QTest::mousePress(widget,Qt::LeftButton,Qt::NoModifier,QPoint(20,26));
    QTest::mouseMove(widget,QPoint(21,26));
    CHECK_FALSE(dragProxy->property("visible").toBool());
    QTest::mouseRelease(widget,Qt::LeftButton,Qt::NoModifier,QPoint(21,26));
}
TEST_CASE("highlights preserve user selection across filter changes") {
    PanelFixture f; f.select(0,"uuid-a");
    f.panel->highlightRows({"uuid-c"});
    f.panel->setSearchFilter("Mod"); f.panel->setSearchFilter("");
    CHECK(f.panel->isHighlighted("uuid-c")); CHECK_FALSE(f.panel->isHighlighted("uuid-a"));
    REQUIRE(f.view());
    CHECK(qmlValue(f.view(),"selectedUuids").toList() == QVariantList{QStringLiteral("uuid-a")});
    CHECK(f.view()->property("currentUuid").toString() == "uuid-a");
}
TEST_CASE("highlight also changes the selected delegate tint") {
    PanelFixture f; f.show(); f.select(0,"uuid-a"); QTest::qWait(1);
    auto colors = [&] {
        QSet<QString> result;
        auto* content = f.view()->property("contentItem").value<QQuickItem*>();
        REQUIRE(content);
        for (auto* item : content->childItems()) if (item->property("color").isValid()) result.insert(item->property("color").value<QColor>().name(QColor::HexArgb));
        return result;
    };
    const auto before = colors();
    f.panel->highlightRows({"uuid-a"}); QTest::qWait(1);
    CHECK(colors() != before);
}
TEST_CASE("QML selection clears when filters hide current UUID and empty state appears") {
    PanelFixture f(false); REQUIRE(f.view());
    CHECK(f.view()->property("currentIndex").toInt() == -1);
    f.select(2,"uuid-c"); f.panel->setSearchFilter("Mod A");
    CHECK(f.view()->property("count").toInt() == 1);
    CHECK(f.view()->property("currentIndex").toInt() == -1);
    CHECK(f.view()->property("anchorIndex").toInt() == -1);
    CHECK(qmlValue(f.view(),"selectedIndices").toList().isEmpty());
    f.panel->setSearchFilter("not present");
    CHECK(f.view()->property("count").toInt() == 0);
    auto* empty = f.root()->findChild<QObject*>(QStringLiteral("emptyStateText")); REQUIRE(empty);
    CHECK(empty->property("visible").toBool());
}
TEST_CASE("QML selection follows UUID during a row move and reveal respects filters") {
    PanelFixture f;
    f.select(2,"uuid-c"); f.panel->moveRow(2,0);
    CHECK(f.panel->uuidAt(0) == "uuid-c"); REQUIRE(f.view());
    CHECK(f.view()->property("currentIndex").toInt() == 0);
    CHECK(qmlValue(f.view(),"selectedIndices").toList() == QVariantList{0});
    CHECK(qmlValue(f.view(),"selectedUuids").toList() == QVariantList{QStringLiteral("uuid-c")});
    CHECK(f.panel->selectUuid("uuid-b"));
    f.panel->setSearchFilter("Mod A"); CHECK_FALSE(f.panel->selectUuid("uuid-b"));
}
TEST_CASE("search focus disables list keyboard actions and Space remains input text") {
    PanelFixture f(false); f.show();
    f.parent.show(); REQUIRE(QTest::qWaitForWindowActive(&f.parent));
    QTest::mouseClick(f.panel->qmlWidget(),Qt::LeftButton,Qt::NoModifier,QPoint(100,26));
    QTest::qWait(1); CHECK(f.root()->property("keyboardActive").toBool());
    f.select(0,"uuid-a"); f.panel->searchInput->setFocus(); QTest::qWait(1);
    CHECK_FALSE(f.root()->property("keyboardActive").toBool());
    QTest::keyClick(f.panel->searchInput,Qt::Key_Return);
    CHECK(f.panel->activeUuids().isEmpty());
    QTest::keyClick(f.panel->searchInput,Qt::Key_Space);
    CHECK(f.panel->searchInput->text() == " ");
    CHECK(f.panel->activeUuids().isEmpty());
}
