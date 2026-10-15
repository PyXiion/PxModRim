#include <catch2/catch_test_macros.hpp>

#include <QPersistentModelIndex>
#include <QSignalSpy>

#include "ui/plugins/organizer/filter_model.h"
#include "ui/plugins/organizer/tree_model.h"

using namespace px::core;
using namespace px::core::organizer;
using px::ui::ModTreeModel;
using px::ui::OrganizerFilterModel;

namespace {
struct OrganizerFixture {
    OrganizerState state;
    ModsByUuid mods;
    QSet<QString> active{QStringLiteral("u1")};

    OrganizerFixture() {
        state.folders.insert(ROOT_ID, {ROOT_ID, std::nullopt, QStringLiteral("Root")});
        auto add = [&](const QString& uuid, const QString& name, const QString& pid, const QString& provider) {
            auto mod = std::make_shared<AboutXmlMod>();
            mod->name = name;
            mod->package_id = CaseInsensitiveStr(pid);
            mod->provider_id = provider;
            mods.insert(uuid, mod);
        };
        add("u1", "One", "a.one", "local");
        add("u2", "Two", "a.two", "steam");
        add("u3", "Loose", "loose.mod", "local");
    }

    void nested() {
        state.folders.insert(2, {2, ROOT_ID, QStringLiteral("Top")});
        state.folders.insert(3, {3, 2, QStringLiteral("Sub")});
        state.placements.insert("a.one", 2);
        state.placements.insert("a.two", 3);
    }

    FolderNode tree(const std::optional<TreeQuery>& query = std::nullopt) const {
        return build_tree(state, mods, active, query);
    }
};

ModTreeModel::SummaryFor no_diagnostics = [](const QString&) -> std::optional<view::ModDiagnosticsView> {
    return std::nullopt;
};
}

TEST_CASE("organizer tree has nested folder rows and trailing ungrouped") {
    OrganizerFixture f;
    f.nested();
    ModTreeModel model(no_diagnostics);
    model.set_tree(f.tree(), f.mods, {}, false);
    REQUIRE(model.rowCount() == 2);
    const auto top = model.index(0, 0);
    const auto ungrouped = model.index(1, 0);
    CHECK(model.data(top).toString() == "Top");
    CHECK(model.data(ungrouped).toString() == "Ungrouped");
    CHECK(model.data(model.index(0, 0, top)).toString() == "Sub");
    CHECK(model.data(model.index(1, 0, top)).toString() == "One");
    CHECK(model.data(model.index(0, 0, ungrouped)).toString() == "Loose");
    CHECK(model.parent(model.index(0, 0, top)) == top);
    CHECK(model.parent(model.index(0, 0, ungrouped)) == ungrouped);
    CHECK_FALSE(model.parent(top).isValid());
    CHECK(model.data(ungrouped, ModTreeModel::TotalCountRole).toInt() == 1);
    CHECK(model.data(top, ModTreeModel::TotalCountRole).toInt() == 2);
    CHECK(model.node_at(top)->folder_id() == 2);
    CHECK(model.node_at(model.index_for_uuid("u2"))->folder_id() == 3);
    CHECK(model.node_at(ungrouped)->folder_id() == ROOT_ID);
    CHECK(model.node_at({}) == nullptr);
    CHECK_FALSE(model.index(-1, 0).isValid());
    CHECK_FALSE(model.index(0, 1).isValid());
    CHECK_FALSE(model.index_for_key("missing").isValid());
    CHECK_FALSE(model.index_for_uuid("missing").isValid());
    CHECK_FALSE(model.data({}).isValid());
    CHECK(model.columnCount() == 1);
}

TEST_CASE("organizer check states retain total counts while filtering") {
    OrganizerFixture f;
    f.state.folders.insert(2, {2, ROOT_ID, QStringLiteral("Group")});
    f.state.placements.insert("a.one", 2);
    f.state.placements.insert("a.two", 2);
    ModTreeModel model(no_diagnostics);
    model.set_tree(f.tree(), f.mods, {}, false);
    auto group = model.index(0, 0);
    CHECK(model.data(group, ModTreeModel::CheckStateRole).toInt() == Qt::PartiallyChecked);
    CHECK(model.data(model.index(0, 0, group), ModTreeModel::CheckStateRole).toInt() == Qt::Checked);
    CHECK(model.data(model.index(1, 0, group), ModTreeModel::CheckStateRole).toInt() == Qt::Unchecked);
    CHECK(model.data(model.index(1, 0), ModTreeModel::CheckStateRole).toInt() == Qt::Unchecked);
    TreeQuery query;
    query.text = QStringLiteral("One");
    model.set_tree(f.tree(query), f.mods, {}, true);
    group = model.index(0, 0);
    CHECK(model.rowCount() == 1);
    CHECK(model.filtering());
    CHECK(model.data(group, ModTreeModel::FilteringRole).toBool());
    CHECK(model.data(group, ModTreeModel::VisibleCountRole).toInt() == 1);
    CHECK(model.data(group, ModTreeModel::TotalCountRole).toInt() == 2);
    CHECK(model.data(group, ModTreeModel::EnabledCountRole).toInt() == 1);
    CHECK(model.data(group, ModTreeModel::CheckStateRole).toInt() == Qt::PartiallyChecked);
    CHECK(model.index_for_key("f:2").isValid());
}

TEST_CASE("organizer issue markers only show on enabled mods") {
    OrganizerFixture f;
    view::ModDiagnosticsView summary{true, true, QStringLiteral("Error"), QStringLiteral("Incompatible")};
    int calls = 0;
    ModTreeModel model([&](const QString&) -> std::optional<view::ModDiagnosticsView> { ++calls; return summary; });
    model.set_tree(f.tree(), f.mods, {}, false);
    const auto enabled = model.index_for_uuid("u1");
    const auto inactive = model.index_for_uuid("u2");
    CHECK(model.parent(enabled) == model.index(0, 0));
    CHECK(model.data(enabled, ModTreeModel::HasErrorRole).toBool());
    CHECK(model.data(enabled, ModTreeModel::HasWarningRole).toBool());
    CHECK(model.data(enabled, ModTreeModel::ErrorTooltipRole).toString() == "Error");
    CHECK(model.data(enabled, ModTreeModel::WarningTooltipRole).toString() == "Incompatible");
    calls = 0;
    CHECK_FALSE(model.data(inactive, ModTreeModel::HasErrorRole).toBool());
    CHECK_FALSE(model.data(inactive, ModTreeModel::HasWarningRole).toBool());
    CHECK(model.data(inactive, ModTreeModel::ErrorTooltipRole).toString().isEmpty());
    CHECK(model.data(inactive, ModTreeModel::WarningTooltipRole).toString().isEmpty());
    CHECK(calls == 0);
    f.active.insert("u2");
    model.set_tree(f.tree(), f.mods, {}, false);
    CHECK(model.data(model.index_for_uuid("u2"), ModTreeModel::HasWarningRole).toBool());
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.refresh_diagnostics();
    REQUIRE(changed.count() == 2);
    CHECK(changed.first()[2].value<QList<int>>() == QList<int>{ModTreeModel::HasErrorRole,
        ModTreeModel::ErrorTooltipRole, ModTreeModel::HasWarningRole, ModTreeModel::WarningTooltipRole});
}

TEST_CASE("organizer same-shape updates preserve persistent indexes and selection") {
    OrganizerFixture f;
    f.nested();
    ModTreeModel model(no_diagnostics);
    model.set_tree(f.tree(), f.mods, {}, false);
    QPersistentModelIndex one(model.index_for_uuid("u1"));
    model.select({one});
    model.select({model.index_for_uuid("u2")}, "extend");
    REQUIRE(model.selected_nodes().size() == 2);
    model.select({model.index_for_uuid("u2")}, "toggle");
    CHECK_FALSE(model.is_selected(model.index_for_uuid("u2")));
    CHECK(model.is_selected(one));
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    f.active.insert("u2");
    model.set_tree(f.tree(), f.mods, {}, false);
    CHECK(reset.isEmpty());
    CHECK(one.isValid());
    CHECK(model.data(one).toString() == "One");
    CHECK(model.data(one, ModTreeModel::SelectedRole).toBool());
    model.clear_selection();
    CHECK(model.selected_nodes().isEmpty());
    model.select({model.index_for_uuid("u3")});
    TreeQuery query;
    query.text = QStringLiteral("One");
    model.set_tree(f.tree(query), f.mods, {}, true);
    CHECK(reset.count() == 1);
    CHECK_FALSE(one.isValid());
    CHECK(model.selected_nodes().isEmpty());
}

TEST_CASE("organizer expansion respects collapsed ancestors except when filtering") {
    OrganizerFixture f;
    f.nested();
    f.state.folders[2].collapsed = true;
    ModTreeModel model(no_diagnostics);
    model.set_tree(f.tree(), f.mods, {}, false);
    auto expanded = model.expanded_indexes();
    REQUIRE(expanded.size() == 1);
    CHECK(model.data(expanded.first(), ModTreeModel::KeyRole).toString() == "ungrouped");
    model.set_tree(f.tree(), f.mods, {}, true);
    expanded = model.expanded_indexes();
    REQUIRE(expanded.size() == 3);
    CHECK(model.data(expanded[0], ModTreeModel::KeyRole).toString() == "f:2");
    CHECK(model.data(expanded[1], ModTreeModel::KeyRole).toString() == "f:3");
    CHECK(model.expanded_indexes(model.index_for_key("f:2")).size() == 1);
}

TEST_CASE("organizer provider metadata tags and rule badges expose QML roles") {
    OrganizerFixture f;
    f.nested();
    f.state.tags.insert(7, {7, QStringLiteral("Later"), QStringLiteral("blue")});
    f.state.tags.insert(2, {2, QStringLiteral("First"), QStringLiteral("red")});
    f.state.mod_tags.insert("a.one", {7, 2, 99});
    auto about = std::dynamic_pointer_cast<AboutXmlMod>(f.mods["u1"]);
    about->mod_version = QStringLiteral("1.2.3");
    auto tree = f.tree();
    tree.children[0].has_rules = true;
    tree.has_rules = true;
    ModTreeModel model(no_diagnostics);
    model.set_tree(tree, f.mods, {{QStringLiteral("local"), QStringLiteral("green")}}, false, f.state.tags);
    const auto one = model.index_for_uuid("u1");
    CHECK(model.data(one, ModTreeModel::UuidRole).toString() == "u1");
    CHECK(model.data(one, ModTreeModel::PackageIdRole).toString() == "a.one");
    CHECK(model.data(one, ModTreeModel::CanPlaceRole).toBool());
    CHECK(model.data(one, ModTreeModel::ProviderLabelRole).toString() == "Local");
    CHECK(model.data(one, ModTreeModel::ProviderIconRole).toString() == "folder");
    CHECK(model.data(one, ModTreeModel::ProviderColorRole).toString() == "green");
    CHECK(model.data(one, ModTreeModel::ModVersionRole).toString() == "1.2.3");
    CHECK(model.data(model.index_for_uuid("u2"), ModTreeModel::ProviderIconRole).toString() == "steam");
    const auto tags = model.data(one, ModTreeModel::TagsRole).toList();
    REQUIRE(tags.size() == 2);
    CHECK(tags[0].toMap()["name"].toString() == "First");
    CHECK(tags[1].toMap()["color"].toString() == "blue");
    CHECK(model.data(model.index_for_key("f:2"), ModTreeModel::HasRuleRole).toBool());
    CHECK_FALSE(model.data(model.index_for_key("ungrouped"), ModTreeModel::HasRuleRole).toBool());
    CHECK(model.roleNames()[ModTreeModel::ProviderLabelRole] == "providerLabel");
    CHECK(model.roleNames()[ModTreeModel::TagsRole] == "tags");

    auto invalid = std::make_shared<ListedMod>();
    invalid->provider_id = QStringLiteral("git");
    f.mods.insert("invalid", invalid);
    model.set_tree(f.tree(), f.mods, {}, false);
    const auto idx = model.index_for_uuid("invalid");
    REQUIRE(idx.isValid());
    CHECK_FALSE(model.data(idx, ModTreeModel::CanPlaceRole).toBool());
    CHECK(model.data(idx, ModTreeModel::PackageIdRole).toString().isEmpty());
    CHECK(model.data(idx, ModTreeModel::ProviderIconRole).toString() == "git");
    CHECK(model.data(idx, ModTreeModel::ModVersionRole).toString().isEmpty());
}

TEST_CASE("organizer filter sections icons and fallbacks match sidebar entries") {
    OrganizerFilterModel model;
    CHECK(model.rowCount() == 0);
    CHECK(model.for_key("missing").key == "all");
    CHECK_FALSE(model.key_at(0));
    QList<TreeFilter> entries{
        {QStringLiteral("all"), QStringLiteral("All"), 10},
        {QStringLiteral("active"), QStringLiteral("Active"), 6, StatusFilter::Active},
        {QStringLiteral("inactive"), QStringLiteral("Inactive"), 4, StatusFilter::Inactive},
        {QStringLiteral("provider:steam"), QStringLiteral("Steam"), 8, StatusFilter::All, QStringLiteral("steam")},
        {QStringLiteral("tag:1"), QStringLiteral("Medieval"), 5, StatusFilter::All, std::nullopt, 1},
        {QStringLiteral("tag:2"), QStringLiteral("Empty"), 0, StatusFilter::All, std::nullopt, 2}};
    model.update(entries);
    REQUIRE(model.rowCount() == 6);
    const QStringList sections{"Status", "Status", "Status", "Providers", "Tags", "Tags"};
    const QStringList icons{"grid", "check-circle", "ban", "steam", "tag", "tag"};
    for (int row = 0; row < 6; ++row) {
        CHECK(model.data(model.index(row), OrganizerFilterModel::SectionRole).toString() == sections[row]);
        CHECK(model.data(model.index(row), OrganizerFilterModel::IconRole).toString() == icons[row]);
    }
    CHECK(model.data(model.index(4), OrganizerFilterModel::KeyRole).toString() == "tag:1");
    CHECK(model.data(model.index(4)).toString() == "Medieval");
    CHECK(model.data(model.index(4), OrganizerFilterModel::LabelRole).toString() == "Medieval");
    CHECK(model.data(model.index(4), OrganizerFilterModel::CountRole).toInt() == 5);
    CHECK(model.data(model.index(5), OrganizerFilterModel::CountRole).toInt() == 0);
    CHECK(model.for_key("tag:1").label == "Medieval");
    CHECK(model.for_key("tag:999").key == "all");
    CHECK(model.key_at(4) == QStringLiteral("tag:1"));
    CHECK_FALSE(model.key_at(-1));
    CHECK_FALSE(model.key_at(6));
    CHECK(model.rowCount(model.index(0)) == 0);
    CHECK_FALSE(model.data({}).isValid());
    CHECK(model.roleNames()[Qt::DisplayRole] == "filterLabel");
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    entries[4].label = QStringLiteral("Medieval Overhaul");
    model.update(entries);
    CHECK(reset.isEmpty());
    CHECK(changed.count() == 1);
    CHECK(model.data(model.index(4)).toString() == "Medieval Overhaul");
    entries.removeLast();
    model.update(entries);
    CHECK(reset.count() == 1);
}

TEST_CASE("organizer filter infers sections from keys and unknown providers use folders") {
    OrganizerFilterModel model;
    model.update({{QStringLiteral("provider:custom"), QStringLiteral("Custom"), 1},
        {QStringLiteral("tag:9"), QStringLiteral("Tag"), 1}});
    CHECK(model.data(model.index(0), OrganizerFilterModel::SectionRole).toString() == "Providers");
    CHECK(model.data(model.index(0), OrganizerFilterModel::IconRole).toString() == "folder");
    CHECK(model.data(model.index(1), OrganizerFilterModel::SectionRole).toString() == "Tags");
    CHECK(model.data(model.index(1), OrganizerFilterModel::IconRole).toString() == "tag");
}
