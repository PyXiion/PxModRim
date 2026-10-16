#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>

#include "core/models/metadata/structures.hpp"
#include "core/organizer/models.hpp"
#include "core/organizer/resolve.hpp"

using namespace px::modrim;
using namespace px::modrim::organizer;

namespace {

ListedModPtr make_mod([[maybe_unused]] const QString& uuid, const QString& name,
                      const std::optional<QString>& pid = std::nullopt,
                      const QString& author = {}, const QString& provider = QStringLiteral("local")) {
    if (!pid) {
        auto mod = std::make_shared<ListedMod>();
        mod->name = name;
        mod->provider_id = provider;
        return mod;
    }
    auto mod = std::make_shared<AboutXmlMod>();
    mod->name = name;
    mod->provider_id = provider;
    mod->package_id = CaseInsensitiveStr(QStringView(*pid));
    if (!author.isEmpty()) {
        mod->authors.append(author);
    }
    return mod;
}

Folder folder(qint64 id, std::optional<qint64> parent_id, QString name) {
    Folder f;
    f.id = id;
    f.parent_id = parent_id;
    f.name = std::move(name);
    f.collapsed = false;
    return f;
}

struct RuleSeed {
    RuleField field;
    RuleOp op;
    QString pattern;
    qint64 folder_id;
};

OrganizerState make_state(QList<Folder> folders = {}, QHash<QString, qint64> placements = {},
                          QList<RuleSeed> rules = {}, QHash<QString, QSet<qint64>> mod_tags = {},
                          QList<Tag> tags = {}) {
    OrganizerState state;
    state.folders.insert(ROOT_ID, folder(ROOT_ID, std::nullopt, QStringLiteral("")));
    for (const Folder& f : folders) {
        state.folders.insert(f.id, f);
    }
    state.placements = std::move(placements);
    state.mod_tags = std::move(mod_tags);
    for (const Tag& tag : tags) {
        state.tags.insert(tag.id, tag);
    }
    for (qsizetype i = 0; i < rules.size(); ++i) {
        const auto& [fld, op, pat, fid] = rules[i];
        state.rules.append(Rule{.id = static_cast<qint64>(i + 1),
                                .position = static_cast<qint64>(i),
                                .field = fld,
                                .op = op,
                                .pattern = pat,
                                .folder_id = fid});
    }
    return state;
}

const FolderNode* find_node(const FolderNode& root, qint64 folder_id) {
    for (const FolderNode* node : root.walk()) {
        if (node->folder.id == folder_id) {
            return node;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("preview_rule_matches counts first matches and assignable ungrouped mods") {
    const OrganizerState state = make_state(
        {folder(2, ROOT_ID, QStringLiteral("A")), folder(3, ROOT_ID, QStringLiteral("B"))},
        {{QStringLiteral("manual.mod"), 3}, {QStringLiteral("root.mod"), ROOT_ID}},
        {{RuleField::PackageId, RuleOp::Prefix, QStringLiteral("old."), 2}});

    const ModsByUuid mods{
        {QStringLiteral("a"), make_mod(QStringLiteral("a"), QStringLiteral("Core Alpha"), QStringLiteral("new.alpha"), QStringLiteral("Author"))},
        {QStringLiteral("b"), make_mod(QStringLiteral("b"), QStringLiteral("Core Beta"), QStringLiteral("old.beta"), QStringLiteral("Other"))},
        {QStringLiteral("c"), make_mod(QStringLiteral("c"), QStringLiteral("Core Manual"), QStringLiteral("manual.mod"), QStringLiteral("Author"))},
        {QStringLiteral("d"), make_mod(QStringLiteral("d"), QStringLiteral("Core Ungrouped"), QStringLiteral("root.mod"), QStringLiteral("Author"))},
        {QStringLiteral("e"), make_mod(QStringLiteral("e"), QStringLiteral("Core No ID"))},
    };

    const QList<RuleSpec> draft{
        RuleSpec{.field = RuleField::Author, .op = RuleOp::Equals, .pattern = QStringLiteral("author"), .folder_id = 3},
        RuleSpec{.field = RuleField::Name, .op = RuleOp::Contains, .pattern = QStringLiteral("core"), .folder_id = 2},
    };

    RulePreview preview = preview_rule_matches(state, mods, draft);
    CHECK(preview.counts == QList<int>{3, 1});
    CHECK(preview.assignable == 1);

    const QList<RuleSpec> reversed{draft[1], draft[0]};
    preview = preview_rule_matches(state, mods, reversed);
    CHECK(preview.counts == QList<int>{4, 0});
    CHECK(preview.assignable == 1);

    QList<RuleSpec> with_blank{
        RuleSpec{.field = RuleField::Name, .op = RuleOp::Contains, .pattern = QStringLiteral("   "), .folder_id = 2},
        draft[0],
        draft[1],
    };
    preview = preview_rule_matches(state, mods, with_blank);
    CHECK(preview.counts == QList<int>{0, 3, 1});
    CHECK(preview.assignable == 1);
}

TEST_CASE("folder_for manual placement beats rules, and root placement blocks rules") {
    SECTION("manual beats rule") {
        const OrganizerState state = make_state(
            {folder(2, ROOT_ID, QStringLiteral("A")), folder(3, ROOT_ID, QStringLiteral("B"))},
            {{QStringLiteral("x.mod"), 3}},
            {{RuleField::PackageId, RuleOp::Prefix, QStringLiteral("x."), 2}});
        const Resolution res = folder_for(state, QStringLiteral("X.Mod"), QStringLiteral("Mod"), QStringLiteral(""));
        CHECK(res.folder_id == 3);
        CHECK(res.placement == Placement::Manual);
    }

    SECTION("root placement blocks rules") {
        const OrganizerState state = make_state(
            {folder(2, ROOT_ID, QStringLiteral("A"))},
            {{QStringLiteral("x.mod"), ROOT_ID}},
            {{RuleField::PackageId, RuleOp::Prefix, QStringLiteral("x."), 2}});
        const Resolution res = folder_for(state, QStringLiteral("x.mod"), QStringLiteral("Mod"), QStringLiteral(""));
        CHECK(res.folder_id == ROOT_ID);
        CHECK(res.placement == Placement::Manual);
    }

    SECTION("first rule by position wins") {
        const OrganizerState state = make_state(
            {folder(2, ROOT_ID, QStringLiteral("A")), folder(3, ROOT_ID, QStringLiteral("B"))}, {},
            {{RuleField::Author, RuleOp::Equals, QStringLiteral("OSKAR"), 3},
             {RuleField::Name, RuleOp::Contains, QStringLiteral("core"), 2}});
        const Resolution res1 = folder_for(state, QStringLiteral("o.core"), QStringLiteral("Core Stuff"), QStringLiteral("Oskar"));
        CHECK(res1.folder_id == 3);
        CHECK(res1.placement == Placement::Rule);
        const Resolution res2 = folder_for(state, QStringLiteral("p.core"), QStringLiteral("Core Stuff"), QStringLiteral("Someone"));
        CHECK(res2.folder_id == 2);
        CHECK(res2.placement == Placement::Rule);
    }

    SECTION("ops are case-insensitive") {
        const OrganizerState state = make_state(
            {folder(2, ROOT_ID, QStringLiteral("A"))}, {},
            {{RuleField::Name, RuleOp::Equals, QStringLiteral("hello world"), 2}});
        CHECK(folder_for(state, QStringLiteral("a.b"), QStringLiteral("Hello World"), QStringLiteral("")).folder_id == 2);
        CHECK(folder_for(state, QStringLiteral("a.b"), QStringLiteral("Hello World!"), QStringLiteral("")).folder_id == ROOT_ID);
    }

    SECTION("no package id or no match is ungrouped") {
        const OrganizerState state = make_state(
            {folder(2, ROOT_ID, QStringLiteral("A"))}, {},
            {{RuleField::Name, RuleOp::Contains, QStringLiteral(""), 2}});
        CHECK(folder_for(state, std::nullopt, QStringLiteral("Anything"), QStringLiteral("")).folder_id == ROOT_ID);
        CHECK(folder_for(state, std::nullopt, QStringLiteral("Anything"), QStringLiteral("")).placement == Placement::None);

        const OrganizerState state2 = make_state(
            {folder(2, ROOT_ID, QStringLiteral("A"))}, {},
            {{RuleField::PackageId, RuleOp::Prefix, QStringLiteral("zzz"), 2}});
        CHECK(folder_for(state2, QStringLiteral("a.b"), QStringLiteral("Mod"), QStringLiteral("")).folder_id == ROOT_ID);
    }
}

namespace {

struct NestedFixture {
    OrganizerState state;
    ModsByUuid mods;
};

NestedFixture make_nested() {
    OrganizerState state = make_state(
        {folder(2, ROOT_ID, QStringLiteral("Zeta")), folder(3, 2, QStringLiteral("inner")),
         folder(4, ROOT_ID, QStringLiteral("alpha"))},
        {{QStringLiteral("a.one"), 2},
         {QStringLiteral("a.two"), 3},
         {QStringLiteral("a.three"), 3},
         {QStringLiteral("b.one"), 4}});
    ModsByUuid mods{
        {QStringLiteral("u1"), make_mod(QStringLiteral("u1"), QStringLiteral("One"), QStringLiteral("a.one"))},
        {QStringLiteral("u2"), make_mod(QStringLiteral("u2"), QStringLiteral("two"), QStringLiteral("a.two"))},
        {QStringLiteral("u3"), make_mod(QStringLiteral("u3"), QStringLiteral("Three"), QStringLiteral("a.three"))},
        {QStringLiteral("u4"), make_mod(QStringLiteral("u4"), QStringLiteral("B One"), QStringLiteral("b.one"))},
        {QStringLiteral("u5"), make_mod(QStringLiteral("u5"), QStringLiteral("loose"))},
    };
    return {std::move(state), std::move(mods)};
}

}  // namespace

TEST_CASE("build_tree computes tri-state checks across nested folders") {
    auto [state, mods] = make_nested();
    const FolderNode root = build_tree(state, mods, {QStringLiteral("u1"), QStringLiteral("u2"), QStringLiteral("u3")});
    const FolderNode* zeta = find_node(root, 2);
    const FolderNode* inner = find_node(root, 3);
    const FolderNode* alpha = find_node(root, 4);
    REQUIRE((zeta && inner && alpha));

    CHECK(inner->total == 2);
    CHECK(inner->enabled == 2);
    CHECK(inner->check == CheckState::On);

    CHECK(zeta->total == 3);
    CHECK(zeta->enabled == 3);
    CHECK(zeta->check == CheckState::On);

    CHECK(alpha->total == 1);
    CHECK(alpha->enabled == 0);
    CHECK(alpha->check == CheckState::Off);

    CHECK(root.total == 5);
    CHECK(root.enabled == 3);
    CHECK(root.check == CheckState::Partial);

    const FolderNode root2 = build_tree(state, mods, {QStringLiteral("u2")});
    CHECK(find_node(root2, 3)->check == CheckState::Partial);
    CHECK(find_node(root2, 2)->check == CheckState::Partial);
}

TEST_CASE("build_tree reports depths and places ungrouped mods at root") {
    auto [state, mods] = make_nested();
    const FolderNode root = build_tree(state, mods, {});
    CHECK(root.depth == 0);
    CHECK(find_node(root, 2)->depth == 1);
    CHECK(find_node(root, 3)->depth == 2);

    REQUIRE(root.mods.size() == 1);
    CHECK(root.mods[0].uuid == QStringLiteral("u5"));
    CHECK(root.mods[0].placement == Placement::None);
    CHECK_FALSE(root.mods[0].package_id.has_value());
}

TEST_CASE("build_tree sorts siblings and mods by name casefolded") {
    auto [state, mods] = make_nested();
    const FolderNode root = build_tree(state, mods, {});
    REQUIRE(root.children.size() == 2);
    CHECK(root.children[0].folder.name == QStringLiteral("alpha"));
    CHECK(root.children[1].folder.name == QStringLiteral("Zeta"));

    const FolderNode* inner = find_node(root, 3);
    REQUIRE(inner->mods.size() == 2);
    CHECK(inner->mods[0].name == QStringLiteral("Three"));
    CHECK(inner->mods[1].name == QStringLiteral("two"));
}

TEST_CASE("build_tree query prunes nodes but counts cover all mods") {
    auto [state, mods] = make_nested();
    TreeQuery query;
    query.text = QStringLiteral("TWO");

    const FolderNode root = build_tree(state, mods, {QStringLiteral("u1")}, query);
    REQUIRE(root.children.size() == 1);
    CHECK(root.children[0].folder.id == 2);

    const FolderNode* zeta = find_node(root, 2);
    const FolderNode* inner = find_node(root, 3);
    REQUIRE((zeta && inner));
    CHECK(zeta->mods.isEmpty());
    REQUIRE(inner->mods.size() == 1);
    CHECK(inner->mods[0].uuid == QStringLiteral("u2"));

    CHECK(zeta->total == 3);
    CHECK(zeta->enabled == 1);
    CHECK(zeta->visible_total == 1);
    CHECK(root.total == 5);
    CHECK(root.visible_total == 1);
    CHECK(root.mods.isEmpty());
}

TEST_CASE("an empty query does not prune empty folders") {
    const OrganizerState state = make_state({folder(2, ROOT_ID, QStringLiteral("Empty"))});
    const FolderNode root = build_tree(state, {}, {}, TreeQuery{});
    REQUIRE(root.children.size() == 1);
    CHECK(root.children[0].folder.id == 2);
    CHECK(find_node(root, 2)->check == CheckState::Off);
}

TEST_CASE("build_tree filters by status, tags, provider and text") {
    const OrganizerState state = make_state(
        {}, {}, {},
        {{QStringLiteral("a.one"), {1, 2}}, {QStringLiteral("a.two"), {1}}});

    const ModsByUuid mods{
        {QStringLiteral("u1"), make_mod(QStringLiteral("u1"), QStringLiteral("One"), QStringLiteral("a.one"), {}, QStringLiteral("steam"))},
        {QStringLiteral("u2"), make_mod(QStringLiteral("u2"), QStringLiteral("Two"), QStringLiteral("a.two"), QStringLiteral("Ludeon"))},
        {QStringLiteral("u3"), make_mod(QStringLiteral("u3"), QStringLiteral("Three"))},
    };

    const auto visible = [&](const TreeQuery& q) {
        const FolderNode root = build_tree(state, mods, {QStringLiteral("u1")}, q);
        QSet<QString> uuids;
        for (const ModLeaf& leaf : root.all_mods()) {
            uuids.insert(leaf.uuid);
        }
        return uuids;
    };

    TreeQuery active_q;
    active_q.status = StatusFilter::Active;
    CHECK(visible(active_q) == QSet<QString>{QStringLiteral("u1")});

    TreeQuery inactive_q;
    inactive_q.status = StatusFilter::Inactive;
    CHECK(visible(inactive_q) == QSet<QString>{QStringLiteral("u2"), QStringLiteral("u3")});

    TreeQuery tag1_q;
    tag1_q.tag_ids = {1};
    CHECK(visible(tag1_q) == QSet<QString>{QStringLiteral("u1"), QStringLiteral("u2")});

    TreeQuery tag12_q;
    tag12_q.tag_ids = {1, 2};
    CHECK(visible(tag12_q) == QSet<QString>{QStringLiteral("u1")});

    TreeQuery steam_q;
    steam_q.provider_ids = {QStringLiteral("steam")};
    CHECK(visible(steam_q) == QSet<QString>{QStringLiteral("u1")});

    TreeQuery text_q;
    text_q.text = QStringLiteral("ludeon");
    CHECK(visible(text_q) == QSet<QString>{QStringLiteral("u2")});

    TreeQuery pid_q;
    pid_q.text = QStringLiteral("a.one");
    CHECK(visible(pid_q) == QSet<QString>{QStringLiteral("u1")});
}

TEST_CASE("mods sharing a package id share their placement and tags") {
    const OrganizerState state = make_state(
        {folder(2, ROOT_ID, QStringLiteral("A"))},
        {{QStringLiteral("dup.mod"), 2}}, {},
        {{QStringLiteral("dup.mod"), {7}}});

    const ModsByUuid mods{
        {QStringLiteral("u1"), make_mod(QStringLiteral("u1"), QStringLiteral("Copy 1"), QStringLiteral("Dup.Mod"))},
        {QStringLiteral("u2"), make_mod(QStringLiteral("u2"), QStringLiteral("Copy 2"), QStringLiteral("dup.mod"))},
    };

    const FolderNode root = build_tree(state, mods, {});
    const FolderNode* a = find_node(root, 2);
    REQUIRE(a != nullptr);
    REQUIRE(a->mods.size() == 2);
    CHECK(a->mods[0].uuid == QStringLiteral("u1"));
    CHECK(a->mods[1].uuid == QStringLiteral("u2"));
    CHECK(a->mods[0].tag_ids == QSet<qint64>{7});
    CHECK(a->mods[1].tag_ids == QSet<qint64>{7});
}

TEST_CASE("build_tree computes own counts and flags folders that have rules") {
    auto [base_state, mods] = make_nested();
    QList<Folder> nested_folders;
    for (const Folder& f : base_state.folders) {
        if (f.id != ROOT_ID) {
            nested_folders.append(f);
        }
    }
    const OrganizerState state = make_state(
        nested_folders, base_state.placements,
        {{RuleField::Name, RuleOp::Contains, QStringLiteral("zzz"), 3}});

    TreeQuery query;
    query.text = QStringLiteral("nothing");
    const FolderNode root = build_tree(state, mods, {QStringLiteral("u5"), QStringLiteral("u1")}, query);
    CHECK(root.own_total == 1);
    CHECK(root.own_enabled == 1);
    CHECK(root.own_check() == CheckState::On);
    CHECK(root.total == 5);
    CHECK(root.check == CheckState::Partial);

    const FolderNode full = build_tree(state, mods, {});
    QList<qint64> ruled_ids;
    for (const FolderNode* node : full.walk()) {
        if (node->has_rules) {
            ruled_ids.append(node->folder.id);
        }
    }
    CHECK(ruled_ids == QList<qint64>{3});
    const FolderNode* zeta = find_node(full, 2);
    REQUIRE(zeta != nullptr);
    CHECK(zeta->own_total == 1);
}

TEST_CASE("TreeFilter::query builds a query matching its preset") {
    TreeFilter tf;
    tf.key = QStringLiteral("tag:10");
    tf.label = QStringLiteral("Medieval");
    tf.count = 3;
    tf.tag_id = 10;

    const TreeQuery q = tf.query(QStringLiteral("sword"));
    CHECK(q.tag_ids == QSet<qint64>{10});
    CHECK(q.text == QStringLiteral("sword"));
    CHECK(q.status == StatusFilter::All);
    CHECK(q.provider_ids.isEmpty());
    CHECK_FALSE(q.is_empty());
}

TEST_CASE("tree_filters preserves order, counts status and counts tags across mods") {
    const Tag t1{.id = 1, .name = QStringLiteral("Medieval"), .color = QStringLiteral("#5eead4")};
    const Tag t2{.id = 2, .name = QStringLiteral("Combat"), .color = QStringLiteral("#fb923c")};
    const Tag t3{.id = 3, .name = QStringLiteral("Magic"), .color = QStringLiteral("#a3e635")};

    const OrganizerState state = make_state(
        {}, {}, {},
        {{QStringLiteral("a.one"), {1, 2}}, {QStringLiteral("a.two"), {1}}},
        {t1, t2, t3});

    const ModsByUuid mods{
        {QStringLiteral("u1"), make_mod(QStringLiteral("u1"), QStringLiteral("One"), QStringLiteral("a.one"), {}, QStringLiteral("steam"))},
        {QStringLiteral("u2"), make_mod(QStringLiteral("u2"), QStringLiteral("Two"), QStringLiteral("a.two"), {}, QStringLiteral("local"))},
        {QStringLiteral("u3"), make_mod(QStringLiteral("u3"), QStringLiteral("Three"), QStringLiteral("a.three"), {}, QStringLiteral("steam"))},
    };

    const QList<TreeFilter> filters = tree_filters(mods, {QStringLiteral("u1")}, &state);
    REQUIRE(filters.size() >= 3);
    CHECK(filters[0].key == QStringLiteral("all"));
    CHECK(filters[0].count == 3);
    CHECK(filters[1].key == QStringLiteral("active"));
    CHECK(filters[1].count == 1);
    CHECK(filters[2].key == QStringLiteral("inactive"));
    CHECK(filters[2].count == 2);

    QList<TreeFilter> providers;
    for (const TreeFilter& f : filters) {
        if (f.provider_id) {
            providers.append(f);
        }
    }
    REQUIRE(providers.size() == 2);
    CHECK(providers[0].key == QStringLiteral("provider:local"));
    CHECK(providers[0].count == 1);
    CHECK(providers[1].key == QStringLiteral("provider:steam"));
    CHECK(providers[1].count == 2);

    QList<TreeFilter> tags;
    for (const TreeFilter& f : filters) {
        if (f.tag_id) {
            tags.append(f);
        }
    }
    REQUIRE(tags.size() == 3);
    CHECK(tags[0].key == QStringLiteral("tag:2"));
    CHECK(tags[0].label == QStringLiteral("Combat"));
    CHECK(tags[0].count == 1);

    CHECK(tags[1].key == QStringLiteral("tag:3"));
    CHECK(tags[1].label == QStringLiteral("Magic"));
    CHECK(tags[1].count == 0);

    CHECK(tags[2].key == QStringLiteral("tag:1"));
    CHECK(tags[2].label == QStringLiteral("Medieval"));
    CHECK(tags[2].count == 2);
}

TEST_CASE("tree_filters counts all copies sharing a package id") {
    const Tag t5{.id = 5, .name = QStringLiteral("Overhaul"), .color = QStringLiteral("#5eead4")};
    const OrganizerState state = make_state({}, {}, {}, {{QStringLiteral("shared.mod"), {5}}}, {t5});

    const ModsByUuid mods{
        {QStringLiteral("u1"), make_mod(QStringLiteral("u1"), QStringLiteral("Steam Copy"), QStringLiteral("Shared.Mod"), {}, QStringLiteral("steam"))},
        {QStringLiteral("u2"), make_mod(QStringLiteral("u2"), QStringLiteral("Local Copy"), QStringLiteral("shared.mod"), {}, QStringLiteral("local"))},
        {QStringLiteral("u3"), make_mod(QStringLiteral("u3"), QStringLiteral("Other Mod"), QStringLiteral("other.mod"), {}, QStringLiteral("local"))},
    };

    const QList<TreeFilter> filters = tree_filters(mods, {}, &state);
    const auto it = std::find_if(filters.cbegin(), filters.cend(), [](const TreeFilter& f) { return f.key == QStringLiteral("tag:5"); });
    REQUIRE(it != filters.cend());
    CHECK(it->count == 2);
    CHECK(it->label == QStringLiteral("Overhaul"));
    CHECK(it->tag_id == 5);
}

TEST_CASE("tree_filters without state or without tags only produces presets and providers") {
    const ModsByUuid mods{{QStringLiteral("u1"), make_mod(QStringLiteral("u1"), QStringLiteral("One"), QStringLiteral("a.one"))}};

    const QList<TreeFilter> without_state = tree_filters(mods, {}, nullptr);
    QStringList keys;
    for (const TreeFilter& f : without_state) {
        keys.append(f.key);
    }
    CHECK(keys == QStringList{QStringLiteral("all"), QStringLiteral("active"), QStringLiteral("inactive"), QStringLiteral("provider:local")});

    const OrganizerState empty_state = make_state();
    const QList<TreeFilter> with_empty_state = tree_filters(mods, {}, &empty_state);
    QStringList empty_keys;
    for (const TreeFilter& f : with_empty_state) {
        empty_keys.append(f.key);
    }
    CHECK(empty_keys == keys);
}

TEST_CASE("filtering tree with a tag query selects only tagged mods") {
    const Tag t1{.id = 1, .name = QStringLiteral("Tagged"), .color = QStringLiteral("#5eead4")};
    const Tag t2{.id = 2, .name = QStringLiteral("Empty"), .color = QStringLiteral("#fb923c")};
    const OrganizerState state = make_state(
        {}, {}, {},
        {{QStringLiteral("a.one"), {1}}, {QStringLiteral("a.two"), {1}}},
        {t1, t2});

    const ModsByUuid mods{
        {QStringLiteral("u1"), make_mod(QStringLiteral("u1"), QStringLiteral("Mod One"), QStringLiteral("a.one"))},
        {QStringLiteral("u2"), make_mod(QStringLiteral("u2"), QStringLiteral("Mod Two"), QStringLiteral("a.two"))},
        {QStringLiteral("u3"), make_mod(QStringLiteral("u3"), QStringLiteral("Mod Three"), QStringLiteral("a.three"))},
    };

    const QList<TreeFilter> filters = tree_filters(mods, {}, &state);
    const auto it1 = std::find_if(filters.cbegin(), filters.cend(), [](const TreeFilter& f) { return f.key == QStringLiteral("tag:1"); });
    REQUIRE(it1 != filters.cend());

    const FolderNode tree1 = build_tree(state, mods, {}, it1->query());
    QSet<QString> visible1;
    for (const ModLeaf& leaf : tree1.all_mods()) {
        visible1.insert(leaf.uuid);
    }
    CHECK(visible1 == QSet<QString>{QStringLiteral("u1"), QStringLiteral("u2")});

    const auto it2 = std::find_if(filters.cbegin(), filters.cend(), [](const TreeFilter& f) { return f.key == QStringLiteral("tag:2"); });
    REQUIRE(it2 != filters.cend());
    const FolderNode empty_tree = build_tree(state, mods, {}, it2->query());
    CHECK(empty_tree.all_mods().isEmpty());
    CHECK(empty_tree.visible_total == 0);

    const FolderNode narrowed = build_tree(state, mods, {}, it1->query(QStringLiteral("Two")));
    QSet<QString> visible_narrowed;
    for (const ModLeaf& leaf : narrowed.all_mods()) {
        visible_narrowed.insert(leaf.uuid);
    }
    CHECK(visible_narrowed == QSet<QString>{QStringLiteral("u2")});
}
