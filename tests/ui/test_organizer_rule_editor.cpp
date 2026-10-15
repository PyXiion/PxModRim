#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <QApplication>
#include <QElapsedTimer>
#include <QJSValue>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QTemporaryDir>
#include <QTest>

#include "test_support.h"
#include "core/context.h"
#include "core/organizer/defaults.h"
#include "ui/components/svg_provider.h"
#include "ui/context.h"
#include "ui/plugins/organizer/tree_model.h"
#include "ui/plugins/organizer/view.h"
#include "ui/theme/qml_theme.h"
#include "../core/organizer/test_util.h"

namespace {
using namespace px::core;
using namespace px::core::organizer;
using namespace px::ui;
using organizer_test::expect;
using organizer_test::wait;

// UI tests may share a QApplication supplied by their test executable.
class Application {
public:
    Application() {
        if (!QCoreApplication::instance()) app_ = std::make_unique<QApplication>(argc_, argv_);
        REQUIRE(qobject_cast<QApplication*>(QCoreApplication::instance()));
    }
private:
    int argc_ = 1;
    char name_[16] = "organizer-test";
    char* argv_[2] = {name_, nullptr};
    std::unique_ptr<QApplication> app_;
};

template<class Predicate>
void until(Predicate predicate) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 5000) QTest::qWait(10);
    REQUIRE(predicate());
}

QQuickItem* item(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems()) {
        if (auto* found = item(child, name)) return found;
    }
    return nullptr;
}

struct Harness {
    Application application;
    QTemporaryDir dir;
    std::unique_ptr<CoreContext> ctx = px::ui_test::makeCore(AppConfig{});
    OrganizerDb db{dir.filePath(QStringLiteral("organizer.db"))};
    OrganizerService* service = nullptr;
    std::unique_ptr<QQmlEngine> engine{createQmlEngine(QCoreApplication::instance())};
    AppContext app{*ctx};
    QWidget parent;
    std::unique_ptr<OrganizerViewPanel> view;
    QQuickItem* editor = nullptr;

    Harness() {
        REQUIRE(dir.isValid());
        auto mod = std::make_shared<AboutXmlMod>();
        mod->name = QStringLiteral("Example");
        mod->provider_id = QStringLiteral("local");
        mod->package_id = CaseInsensitiveStr(QStringView(u"example.mod"));
        ctx->load({{QStringLiteral("example"), mod}}, {});
        auto plugin = std::make_unique<OrganizerService>(*ctx, db);
        service = plugin.get();
        ctx->registerPlugin(std::move(plugin));
        wait(service->init());
        engine->rootContext()->setContextProperty(QStringLiteral("Theme"), new Theme(engine.get()));
        view = std::make_unique<OrganizerViewPanel>(*ctx, engine.get(), &parent, app);
        parent.resize(920, 700);
        view->resize(parent.size());
        parent.show();
        view->show();
        view->rebuild();
    }

    ~Harness() {
        view.reset();
        wait(service->shutdown());
    }

    QJSValue call(const QString& expression) {
        engine->globalObject().setProperty(QStringLiteral("editor"), engine->newQObject(editor));
        auto result = engine->evaluate(QStringLiteral("editor.") + expression);
        INFO(result.toString().toStdString());
        REQUIRE_FALSE(result.isError());
        return result;
    }

    void open(int count) {
        view->openRules();
        auto* quick = view->findChild<QQuickWidget*>(QStringLiteral("organizerRuleEditorQuickWidget"));
        REQUIRE(quick);
        until([&] { return quick->status() != QQuickWidget::Loading; });
        INFO((quick->errors().isEmpty() ? std::string{} : quick->errors().first().toString().toStdString()));
        REQUIRE(quick->status() == QQuickWidget::Ready);
        editor = quick->rootObject();
        REQUIRE(editor);
        until([&] { return editor->property("opened").toBool() && draft().size() == count; });
    }

    QVariantList draft() { return call(QStringLiteral("draftRules()")).toVariant().toList(); }

    void pattern(int index, const QString& value) {
        REQUIRE(QMetaObject::invokeMethod(editor, "setRulePattern", Q_ARG(QVariant, index), Q_ARG(QVariant, value)));
    }

    QQuickItem* row(int index) {
        QQuickItem* result = nullptr;
        until([&] {
            auto visit = [&](auto&& self, QQuickItem* node) -> void {
                if (node->objectName() == QStringLiteral("organizerRuleRow") && node->property("index").toInt() == index) result = node;
                for (auto* child : node->childItems()) self(self, child);
            };
            visit(visit, editor);
            return result != nullptr;
        });
        return result;
    }

    void choose(int index, const QString& name, int choice) {
        auto* control = item(row(index), name);
        REQUIRE(control);
        REQUIRE(control->setProperty("currentIndex", choice));
        REQUIRE(QMetaObject::invokeMethod(control, "activated", Q_ARG(int, choice)));
    }

    void save() {
        call(QStringLiteral("submitRules()"));
        until([&] { return !editor->property("opened").toBool(); });
    }
};

void checkRule(const Rule& rule, RuleOp op, const QString& pattern, qint64 folder) {
    CHECK(rule.field == RuleField::PackageId);
    CHECK(rule.op == op);
    CHECK(rule.pattern == pattern);
    CHECK(rule.folder_id == folder);
}
} // namespace

TEST_CASE("organizer rule editor preserves order and retains an invalid draft", "[ui][organizer]") {
    Harness h;
    const auto first = expect(wait(h.service->create_folder(QStringLiteral("First"))));
    const auto second = expect(wait(h.service->create_folder(QStringLiteral("Second"))));
    h.view->rebuild();
    h.open(0);
    h.call(QStringLiteral("addRule()"));
    h.pattern(0, QStringLiteral("example"));
    h.save();
    auto state = expect(wait(h.db.load()));
    REQUIRE(state.rules.size() == 1);
    checkRule(state.rules[0], RuleOp::Prefix, QStringLiteral("example"), first.id);
    CHECK(expect(h.service->folder_mod_uuids(first.id)) == QStringList{QStringLiteral("example")});

    h.open(1);
    h.call(QStringLiteral("addRule()"));
    h.pattern(1, QStringLiteral("example.mod"));
    h.choose(1, QStringLiteral("organizerRuleOperator"), 2);
    const auto folders = h.view->ruleFolders();
    const auto target = std::find_if(folders.begin(), folders.end(), [&](const QVariant& value) {
        return value.toMap().value(QStringLiteral("id")).toLongLong() == second.id;
    });
    REQUIRE(target != folders.end());
    h.choose(1, QStringLiteral("organizerRuleFolder"), static_cast<int>(target - folders.begin()));
    h.call(QStringLiteral("moveUp(1)"));
    REQUIRE(h.draft()[0].toMap().value(QStringLiteral("pattern")).toString() == QStringLiteral("example.mod"));
    h.save();
    state = expect(wait(h.db.load()));
    REQUIRE(state.rules.size() == 2);
    checkRule(state.rules[0], RuleOp::Equals, QStringLiteral("example.mod"), second.id);
    checkRule(state.rules[1], RuleOp::Prefix, QStringLiteral("example"), first.id);
    CHECK(expect(h.service->folder_mod_uuids(second.id)) == QStringList{QStringLiteral("example")});

    const auto saved = state.rules;
    h.open(2);
    h.pattern(0, QStringLiteral("   "));
    h.call(QStringLiteral("submitRules()"));
    until([&] { return h.editor->property("errorMessage").toString().contains(QStringLiteral("pattern cannot be empty")); });
    CHECK(h.editor->property("opened").toBool());
    CHECK(h.draft()[0].toMap().value(QStringLiteral("pattern")).toString() == QStringLiteral("   "));
    CHECK(h.draft()[1].toMap().value(QStringLiteral("pattern")).toString() == QStringLiteral("example"));
    CHECK(expect(wait(h.db.load())).rules == saved);
    h.call(QStringLiteral("removeRule(0)"));
    h.save();
    state = expect(wait(h.db.load()));
    REQUIRE(state.rules.size() == 1);
    checkRule(state.rules[0], RuleOp::Prefix, QStringLiteral("example"), first.id);
    CHECK(expect(h.service->folder_mod_uuids(first.id)) == QStringList{QStringLiteral("example")});
}

TEST_CASE("organizer standard rules refresh only a clean editor draft", "[ui][organizer]") {
    Harness h;
    expect(wait(h.service->create_folder(QStringLiteral("Mine"))));
    h.view->rebuild();
    h.open(0);
    auto* standard = item(h.editor, QStringLiteral("organizerAddStandardRules"));
    REQUIRE(standard);
    REQUIRE(standard->property("enabled").toBool());
    REQUIRE(QMetaObject::invokeMethod(standard, "clicked"));
    until([&] { return h.draft().size() == standard_rules().size(); });
    CHECK(h.editor->property("opened").toBool());
    CHECK(h.editor->property("errorMessage").toString().isEmpty());
    CHECK_FALSE(h.editor->property("draftDirty").toBool());
    const auto state = expect(wait(h.db.load()));
    REQUIRE(state.rules.size() == standard_rules().size());
    for (qsizetype i = 0; i < state.rules.size(); ++i) {
        const auto& actual = state.rules[i];
        const auto& expected = standard_rules()[i];
        CHECK(state.folders.value(actual.folder_id).name == expected.folder);
        CHECK(actual.field == expected.field);
        CHECK(actual.op == expected.op);
        CHECK(actual.pattern == expected.pattern);
        CHECK(h.draft()[i].toMap().value(QStringLiteral("pattern")).toString() == expected.pattern);
    }
    REQUIRE(standard->property("enabled").toBool());
    h.pattern(0, QStringLiteral("edited"));
    until([&] { return !standard->property("enabled").toBool(); });
    CHECK(h.editor->property("draftDirty").toBool());
    CHECK(expect(wait(h.db.load())).rules == state.rules);
}

TEST_CASE("organizer tag actions persist assignments and validated edits", "[ui][organizer]") {
    Harness h;
    auto second = std::make_shared<AboutXmlMod>();
    second->name = QStringLiteral("Second");
    second->provider_id = QStringLiteral("local");
    second->package_id = CaseInsensitiveStr(QStringView(u"second.mod"));
    auto mods = h.ctx->allMods();
    mods.insert(QStringLiteral("second"), second);
    h.ctx->load(std::move(mods), {});
    h.view->rebuild();
    auto* model = h.view->findChild<ModTreeModel*>();
    REQUIRE(model);
    const auto select = [&](bool both) {
        const auto first = model->index_for_uuid(QStringLiteral("example"));
        REQUIRE(first.isValid());
        h.view->selectSingle(first);
        if (both) {
            const auto other = model->index_for_uuid(QStringLiteral("second"));
            REQUIRE(other.isValid());
            h.view->selectRow(other, static_cast<int>(Qt::ControlModifier), {});
        }
    };
    // Invoke through QML so QmlTask-returning actions follow the real call path.
    h.engine->globalObject().setProperty(QStringLiteral("panel"), h.engine->newQObject(h.view.get()));
    const auto action = [&](const QString& expression) {
        const auto result = h.engine->evaluate(QStringLiteral("panel.") + expression);
        INFO(result.toString().toStdString());
        REQUIRE_FALSE(result.isError());
    };
    select(false);
    action(QStringLiteral("createTag('Favorites', 'not-a-color')"));
    until([&] { return h.view->editorError().contains(QStringLiteral("valid color")); });
    CHECK(h.service->state().tags.isEmpty());
    action(QStringLiteral("createTag('Favorites', '#3399aa')"));
    until([&] { return h.service->state().tags.size() == 1; });
    const auto id = h.service->state().tags.constBegin().key();
    CHECK(h.service->state().tags.value(id).name == QStringLiteral("Favorites"));
    CHECK(h.service->state().tags.value(id).color == QStringLiteral("#3399aa"));
    CHECK(h.view->editorError().isEmpty());
    const auto assign = [&](bool add) {
        action(QStringLiteral("assignTag(%1, %2)").arg(id).arg(add ? QStringLiteral("true") : QStringLiteral("false")));
    };
    const auto checkState = [&] {
        const auto rows = h.view->tagRows();
        REQUIRE(rows.size() == 1);
        return rows[0].toMap().value(QStringLiteral("checkState")).toInt();
    };
    CHECK(checkState() == 0);
    assign(true);
    until([&] { return h.service->state().mod_tags.value(QStringLiteral("example.mod")).contains(id); });
    CHECK(checkState() == 1);
    select(true);
    CHECK(checkState() == 2);
    assign(true);
    until([&] { return h.service->state().mod_tags.value(QStringLiteral("second.mod")).contains(id); });
    CHECK(checkState() == 1);
    auto persisted = expect(wait(h.db.load()));
    CHECK(persisted.mod_tags.value(QStringLiteral("example.mod")) == QSet<qint64>{id});
    CHECK(persisted.mod_tags.value(QStringLiteral("second.mod")) == QSet<qint64>{id});
    assign(false);
    until([&] {
        return h.service->state().mod_tags.value(QStringLiteral("example.mod")).isEmpty()
            && h.service->state().mod_tags.value(QStringLiteral("second.mod")).isEmpty();
    });
    CHECK(checkState() == 0);
    action(QStringLiteral("updateTag(%1, 'Best', 'bad')").arg(id));
    until([&] { return h.view->editorError().contains(QStringLiteral("valid color")); });
    CHECK(h.service->state().tags.value(id).name == QStringLiteral("Favorites"));
    CHECK(expect(wait(h.db.load())).tags.value(id).color == QStringLiteral("#3399aa"));
    action(QStringLiteral("updateTag(%1, 'Best', '#123456')").arg(id));
    until([&] { return h.service->state().tags.value(id).name == QStringLiteral("Best"); });
    CHECK(h.view->editorError().isEmpty());
    persisted = expect(wait(h.db.load()));
    CHECK(persisted.tags.value(id).name == QStringLiteral("Best"));
    CHECK(persisted.tags.value(id).color == QStringLiteral("#123456"));
    CHECK(persisted.mod_tags.value(QStringLiteral("example.mod")).isEmpty());
    CHECK(persisted.mod_tags.value(QStringLiteral("second.mod")).isEmpty());
    action(QStringLiteral("deleteTag(%1)").arg(id));
    until([&] { return h.service->state().tags.isEmpty(); });
    CHECK(expect(wait(h.db.load())).tags.isEmpty());
}

TEST_CASE("organizer folder checkbox toggles descendants with one active-state change", "[ui][organizer]") {
    Harness h;
    auto second = std::make_shared<AboutXmlMod>();
    second->name = QStringLiteral("Second");
    second->provider_id = QStringLiteral("local");
    second->package_id = CaseInsensitiveStr(QStringView(u"second.mod"));
    auto mods = h.ctx->allMods();
    mods.insert(QStringLiteral("second"), second);
    h.ctx->load(std::move(mods), {});
    const auto parent = expect(wait(h.service->create_folder(QStringLiteral("Parent"))));
    const auto child = expect(wait(h.service->create_folder(QStringLiteral("Child"), parent.id)));
    expect(wait(h.service->place({QStringLiteral("example.mod")}, child.id)));
    expect(wait(h.service->place({QStringLiteral("second.mod")}, parent.id)));
    h.view->rebuild();
    auto* model = h.view->findChild<ModTreeModel*>();
    REQUIRE(model);
    int changes = 0;
    const auto connection = h.ctx->activeStateChanged().connect([&](const QStringList&) { ++changes; });
    const auto toggle = [&] {
        const auto index = model->index_for_key(QStringLiteral("f:%1").arg(parent.id));
        REQUIRE(index.isValid());
        h.view->toggleCheck(index);
    };
    toggle();
    until([&] { return h.ctx->activeUuids().size() == 2; });
    CHECK(h.ctx->activeUuids().contains(QStringLiteral("example")));
    CHECK(h.ctx->activeUuids().contains(QStringLiteral("second")));
    CHECK(changes == 1);
    h.view->rebuild();
    toggle();
    until([&] { return h.ctx->activeUuids().isEmpty(); });
    CHECK(changes == 2);
    h.ctx->activeStateChanged().disconnect(connection);
}
