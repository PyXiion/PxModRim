#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>

#include "core/models/metadata/structures.hpp"
#include "core/organizer/models.hpp"
#include "core/organizer/resolve.hpp"

using namespace px::modrim;
using namespace px::modrim::organizer;

namespace {

ListedModPtr makeMod([[maybe_unused]] const QString& uuid, const QString& name,
                      const std::optional<QString>& pid = std::nullopt,
                      const QString& author = {}, const QString& provider = QStringLiteral("local")) {
    if (!pid) {
        auto mod = std::make_shared<ListedMod>();
        mod->name = name;
        mod->providerId = provider;
        return mod;
    }
    auto mod = std::make_shared<AboutXmlMod>();
    mod->name = name;
    mod->providerId = provider;
    mod->packageId = CaseInsensitiveStr(QStringView(*pid));
    if (!author.isEmpty()) {
        mod->authors.append(author);
    }
    return mod;
}

Folder folder(qint64 id, std::optional<qint64> parentId, QString name) {
    Folder f;
    f.id = id;
    f.parentId = parentId;
    f.name = std::move(name);
    f.collapsed = false;
    return f;
}

struct RuleSeed {
    RuleField field;
    RuleOp op;
    QString pattern;
    qint64 folderId;
};

OrganizerState makeState(QList<Folder> folders = {}, QHash<QString, qint64> placements = {},
                          QList<RuleSeed> rules = {}, QHash<QString, QSet<qint64>> modTags = {},
                          QList<Tag> tags = {}) {
    OrganizerState state;
    state.folders.insert(kRootId, folder(kRootId, std::nullopt, QStringLiteral("")));
    for (const Folder& f : folders) {
        state.folders.insert(f.id, f);
    }
    state.placements = std::move(placements);
    state.modTags = std::move(modTags);
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
                                .folderId = fid});
    }
    return state;
}

const FolderNode* findNode(const FolderNode& root, qint64 folderId) {
    for (const FolderNode* node : root.walk()) {
        if (node->folder.id == folderId) {
            return node;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("preview_rule_matches counts first matches and assignable ungrouped mods") {
    const OrganizerState state = makeState(
        {folder(2, kRootId, QStringLiteral("A")), folder(3, kRootId, QStringLiteral("B"))},
        {{QStringLiteral("manual.mod"), 3}, {QStringLiteral("root.mod"), kRootId}},
        {{RuleField::kPackageId, RuleOp::kPrefix, QStringLiteral("old."), 2}});

    const ModsByUuid mods{
        {QStringLiteral("a"), makeMod(QStringLiteral("a"), QStringLiteral("Core Alpha"), QStringLiteral("new.alpha"), QStringLiteral("Author"))},
        {QStringLiteral("b"), makeMod(QStringLiteral("b"), QStringLiteral("Core Beta"), QStringLiteral("old.beta"), QStringLiteral("Other"))},
        {QStringLiteral("c"), makeMod(QStringLiteral("c"), QStringLiteral("Core Manual"), QStringLiteral("manual.mod"), QStringLiteral("Author"))},
        {QStringLiteral("d"), makeMod(QStringLiteral("d"), QStringLiteral("Core Ungrouped"), QStringLiteral("root.mod"), QStringLiteral("Author"))},
        {QStringLiteral("e"), makeMod(QStringLiteral("e"), QStringLiteral("Core No ID"))},
    };

    const QList<RuleSpec> draft{
        RuleSpec{.field = RuleField::kAuthor, .op = RuleOp::kEquals, .pattern = QStringLiteral("author"), .folderId = 3},
        RuleSpec{.field = RuleField::kName, .op = RuleOp::kContains, .pattern = QStringLiteral("core"), .folderId = 2},
    };

    RulePreview preview = previewRuleMatches(state, mods, draft);
    CHECK(preview.counts == QList<int>{3, 1});
    CHECK(preview.assignable == 1);

    const QList<RuleSpec> reversed{draft[1], draft[0]};
    preview = previewRuleMatches(state, mods, reversed);
    CHECK(preview.counts == QList<int>{4, 0});
    CHECK(preview.assignable == 1);

    QList<RuleSpec> withBlank{
        RuleSpec{.field = RuleField::kName, .op = RuleOp::kContains, .pattern = QStringLiteral("   "), .folderId = 2},
        draft[0],
        draft[1],
    };
    preview = previewRuleMatches(state, mods, withBlank);
    CHECK(preview.counts == QList<int>{0, 3, 1});
    CHECK(preview.assignable == 1);
}

TEST_CASE("folder_for manual placement beats rules, and root placement blocks rules") {
    SECTION("manual beats rule") {
        const OrganizerState state = makeState(
            {folder(2, kRootId, QStringLiteral("A")), folder(3, kRootId, QStringLiteral("B"))},
            {{QStringLiteral("x.mod"), 3}},
            {{RuleField::kPackageId, RuleOp::kPrefix, QStringLiteral("x."), 2}});
        const Resolution res = folderFor(state, QStringLiteral("X.Mod"), QStringLiteral("Mod"), QStringLiteral(""));
        CHECK(res.folderId == 3);
        CHECK(res.placement == Placement::kManual);
    }

    SECTION("root placement blocks rules") {
        const OrganizerState state = makeState(
            {folder(2, kRootId, QStringLiteral("A"))},
            {{QStringLiteral("x.mod"), kRootId}},
            {{RuleField::kPackageId, RuleOp::kPrefix, QStringLiteral("x."), 2}});
        const Resolution res = folderFor(state, QStringLiteral("x.mod"), QStringLiteral("Mod"), QStringLiteral(""));
        CHECK(res.folderId == kRootId);
        CHECK(res.placement == Placement::kManual);
    }

    SECTION("first rule by position wins") {
        const OrganizerState state = makeState(
            {folder(2, kRootId, QStringLiteral("A")), folder(3, kRootId, QStringLiteral("B"))}, {},
            {{RuleField::kAuthor, RuleOp::kEquals, QStringLiteral("OSKAR"), 3},
             {RuleField::kName, RuleOp::kContains, QStringLiteral("core"), 2}});
        const Resolution res1 = folderFor(state, QStringLiteral("o.core"), QStringLiteral("Core Stuff"), QStringLiteral("Oskar"));
        CHECK(res1.folderId == 3);
        CHECK(res1.placement == Placement::kRule);
        const Resolution res2 = folderFor(state, QStringLiteral("p.core"), QStringLiteral("Core Stuff"), QStringLiteral("Someone"));
        CHECK(res2.folderId == 2);
        CHECK(res2.placement == Placement::kRule);
    }

    SECTION("ops are case-insensitive") {
        const OrganizerState state = makeState(
            {folder(2, kRootId, QStringLiteral("A"))}, {},
            {{RuleField::kName, RuleOp::kEquals, QStringLiteral("hello world"), 2}});
        CHECK(folderFor(state, QStringLiteral("a.b"), QStringLiteral("Hello World"), QStringLiteral("")).folderId == 2);
        CHECK(folderFor(state, QStringLiteral("a.b"), QStringLiteral("Hello World!"), QStringLiteral("")).folderId == kRootId);
    }

    SECTION("no package id or no match is ungrouped") {
        const OrganizerState state = makeState(
            {folder(2, kRootId, QStringLiteral("A"))}, {},
            {{RuleField::kName, RuleOp::kContains, QStringLiteral(""), 2}});
        CHECK(folderFor(state, std::nullopt, QStringLiteral("Anything"), QStringLiteral("")).folderId == kRootId);
        CHECK(folderFor(state, std::nullopt, QStringLiteral("Anything"), QStringLiteral("")).placement == Placement::kNone);

        const OrganizerState state2 = makeState(
            {folder(2, kRootId, QStringLiteral("A"))}, {},
            {{RuleField::kPackageId, RuleOp::kPrefix, QStringLiteral("zzz"), 2}});
        CHECK(folderFor(state2, QStringLiteral("a.b"), QStringLiteral("Mod"), QStringLiteral("")).folderId == kRootId);
    }
}

namespace {

struct NestedFixture {
    OrganizerState state;
    ModsByUuid mods;
};

NestedFixture makeNested() {
    OrganizerState state = makeState(
        {folder(2, kRootId, QStringLiteral("Zeta")), folder(3, 2, QStringLiteral("inner")),
         folder(4, kRootId, QStringLiteral("alpha"))},
        {{QStringLiteral("a.one"), 2},
         {QStringLiteral("a.two"), 3},
         {QStringLiteral("a.three"), 3},
         {QStringLiteral("b.one"), 4}});
    ModsByUuid mods{
        {QStringLiteral("u1"), makeMod(QStringLiteral("u1"), QStringLiteral("One"), QStringLiteral("a.one"))},
        {QStringLiteral("u2"), makeMod(QStringLiteral("u2"), QStringLiteral("two"), QStringLiteral("a.two"))},
        {QStringLiteral("u3"), makeMod(QStringLiteral("u3"), QStringLiteral("Three"), QStringLiteral("a.three"))},
        {QStringLiteral("u4"), makeMod(QStringLiteral("u4"), QStringLiteral("B One"), QStringLiteral("b.one"))},
        {QStringLiteral("u5"), makeMod(QStringLiteral("u5"), QStringLiteral("loose"))},
    };
    return {std::move(state), std::move(mods)};
}

}  // namespace

TEST_CASE("build_tree computes tri-state checks across nested folders") {
    auto [state, mods] = makeNested();
    const FolderNode root = buildTree(state, mods, {QStringLiteral("u1"), QStringLiteral("u2"), QStringLiteral("u3")});
    const FolderNode* zeta = findNode(root, 2);
    const FolderNode* inner = findNode(root, 3);
    const FolderNode* alpha = findNode(root, 4);
    REQUIRE((zeta && inner && alpha));

    CHECK(inner->total == 2);
    CHECK(inner->enabled == 2);
    CHECK(inner->check == CheckState::kOn);

    CHECK(zeta->total == 3);
    CHECK(zeta->enabled == 3);
    CHECK(zeta->check == CheckState::kOn);

    CHECK(alpha->total == 1);
    CHECK(alpha->enabled == 0);
    CHECK(alpha->check == CheckState::kOff);

    CHECK(root.total == 5);
    CHECK(root.enabled == 3);
    CHECK(root.check == CheckState::kPartial);

    const FolderNode root2 = buildTree(state, mods, {QStringLiteral("u2")});
    CHECK(findNode(root2, 3)->check == CheckState::kPartial);
    CHECK(findNode(root2, 2)->check == CheckState::kPartial);
}

TEST_CASE("build_tree reports depths and places ungrouped mods at root") {
    auto [state, mods] = makeNested();
    const FolderNode root = buildTree(state, mods, {});
    CHECK(root.depth == 0);
    CHECK(findNode(root, 2)->depth == 1);
    CHECK(findNode(root, 3)->depth == 2);

    REQUIRE(root.mods.size() == 1);
    CHECK(root.mods[0].uuid == QStringLiteral("u5"));
    CHECK(root.mods[0].placement == Placement::kNone);
    CHECK_FALSE(root.mods[0].packageId.has_value());
}

TEST_CASE("build_tree sorts siblings and mods by name casefolded") {
    auto [state, mods] = makeNested();
    const FolderNode root = buildTree(state, mods, {});
    REQUIRE(root.children.size() == 2);
    CHECK(root.children[0].folder.name == QStringLiteral("alpha"));
    CHECK(root.children[1].folder.name == QStringLiteral("Zeta"));

    const FolderNode* inner = findNode(root, 3);
    REQUIRE(inner->mods.size() == 2);
    CHECK(inner->mods[0].name == QStringLiteral("Three"));
    CHECK(inner->mods[1].name == QStringLiteral("two"));
}

TEST_CASE("build_tree query prunes nodes but counts cover all mods") {
    auto [state, mods] = makeNested();
    TreeQuery query;
    query.text = QStringLiteral("TWO");

    const FolderNode root = buildTree(state, mods, {QStringLiteral("u1")}, query);
    REQUIRE(root.children.size() == 1);
    CHECK(root.children[0].folder.id == 2);

    const FolderNode* zeta = findNode(root, 2);
    const FolderNode* inner = findNode(root, 3);
    REQUIRE((zeta && inner));
    CHECK(zeta->mods.isEmpty());
    REQUIRE(inner->mods.size() == 1);
    CHECK(inner->mods[0].uuid == QStringLiteral("u2"));

    CHECK(zeta->total == 3);
    CHECK(zeta->enabled == 1);
    CHECK(zeta->visibleTotal == 1);
    CHECK(root.total == 5);
    CHECK(root.visibleTotal == 1);
    CHECK(root.mods.isEmpty());
}

TEST_CASE("an empty query does not prune empty folders") {
    const OrganizerState state = makeState({folder(2, kRootId, QStringLiteral("Empty"))});
    const FolderNode root = buildTree(state, {}, {}, TreeQuery{});
    REQUIRE(root.children.size() == 1);
    CHECK(root.children[0].folder.id == 2);
    CHECK(findNode(root, 2)->check == CheckState::kOff);
}

TEST_CASE("build_tree filters by status, tags, provider and text") {
    const OrganizerState state = makeState(
        {}, {}, {},
        {{QStringLiteral("a.one"), {1, 2}}, {QStringLiteral("a.two"), {1}}});

    const ModsByUuid mods{
        {QStringLiteral("u1"), makeMod(QStringLiteral("u1"), QStringLiteral("One"), QStringLiteral("a.one"), {}, QStringLiteral("steam"))},
        {QStringLiteral("u2"), makeMod(QStringLiteral("u2"), QStringLiteral("Two"), QStringLiteral("a.two"), QStringLiteral("Ludeon"))},
        {QStringLiteral("u3"), makeMod(QStringLiteral("u3"), QStringLiteral("Three"))},
    };

    const auto visible = [&](const TreeQuery& q) {
        const FolderNode root = buildTree(state, mods, {QStringLiteral("u1")}, q);
        QSet<QString> uuids;
        for (const ModLeaf& leaf : root.allMods()) {
            uuids.insert(leaf.uuid);
        }
        return uuids;
    };

    TreeQuery activeQ;
    activeQ.status = StatusFilter::kActive;
    CHECK(visible(activeQ) == QSet<QString>{QStringLiteral("u1")});

    TreeQuery inactiveQ;
    inactiveQ.status = StatusFilter::kInactive;
    CHECK(visible(inactiveQ) == QSet<QString>{QStringLiteral("u2"), QStringLiteral("u3")});

    TreeQuery tag1Q;
    tag1Q.tagIds = {1};
    CHECK(visible(tag1Q) == QSet<QString>{QStringLiteral("u1"), QStringLiteral("u2")});

    TreeQuery tag12Q;
    tag12Q.tagIds = {1, 2};
    CHECK(visible(tag12Q) == QSet<QString>{QStringLiteral("u1")});

    TreeQuery steamQ;
    steamQ.providerIds = {QStringLiteral("steam")};
    CHECK(visible(steamQ) == QSet<QString>{QStringLiteral("u1")});

    TreeQuery textQ;
    textQ.text = QStringLiteral("ludeon");
    CHECK(visible(textQ) == QSet<QString>{QStringLiteral("u2")});

    TreeQuery pidQ;
    pidQ.text = QStringLiteral("a.one");
    CHECK(visible(pidQ) == QSet<QString>{QStringLiteral("u1")});
}

TEST_CASE("mods sharing a package id share their placement and tags") {
    const OrganizerState state = makeState(
        {folder(2, kRootId, QStringLiteral("A"))},
        {{QStringLiteral("dup.mod"), 2}}, {},
        {{QStringLiteral("dup.mod"), {7}}});

    const ModsByUuid mods{
        {QStringLiteral("u1"), makeMod(QStringLiteral("u1"), QStringLiteral("Copy 1"), QStringLiteral("Dup.Mod"))},
        {QStringLiteral("u2"), makeMod(QStringLiteral("u2"), QStringLiteral("Copy 2"), QStringLiteral("dup.mod"))},
    };

    const FolderNode root = buildTree(state, mods, {});
    const FolderNode* a = findNode(root, 2);
    REQUIRE(a != nullptr);
    REQUIRE(a->mods.size() == 2);
    CHECK(a->mods[0].uuid == QStringLiteral("u1"));
    CHECK(a->mods[1].uuid == QStringLiteral("u2"));
    CHECK(a->mods[0].tagIds == QSet<qint64>{7});
    CHECK(a->mods[1].tagIds == QSet<qint64>{7});
}

TEST_CASE("build_tree computes own counts and flags folders that have rules") {
    auto [base_state, mods] = makeNested();
    QList<Folder> nestedFolders;
    for (const Folder& f : base_state.folders) {
        if (f.id != kRootId) {
            nestedFolders.append(f);
        }
    }
    const OrganizerState state = makeState(
        nestedFolders, base_state.placements,
        {{RuleField::kName, RuleOp::kContains, QStringLiteral("zzz"), 3}});

    TreeQuery query;
    query.text = QStringLiteral("nothing");
    const FolderNode root = buildTree(state, mods, {QStringLiteral("u5"), QStringLiteral("u1")}, query);
    CHECK(root.ownTotal == 1);
    CHECK(root.ownEnabled == 1);
    CHECK(root.ownCheck() == CheckState::kOn);
    CHECK(root.total == 5);
    CHECK(root.check == CheckState::kPartial);

    const FolderNode full = buildTree(state, mods, {});
    QList<qint64> ruledIds;
    for (const FolderNode* node : full.walk()) {
        if (node->hasRules) {
            ruledIds.append(node->folder.id);
        }
    }
    CHECK(ruledIds == QList<qint64>{3});
    const FolderNode* zeta = findNode(full, 2);
    REQUIRE(zeta != nullptr);
    CHECK(zeta->ownTotal == 1);
}

TEST_CASE("TreeFilter::query builds a query matching its preset") {
    TreeFilter tf;
    tf.key = QStringLiteral("tag:10");
    tf.label = QStringLiteral("Medieval");
    tf.count = 3;
    tf.tagId = 10;

    const TreeQuery q = tf.query(QStringLiteral("sword"));
    CHECK(q.tagIds == QSet<qint64>{10});
    CHECK(q.text == QStringLiteral("sword"));
    CHECK(q.status == StatusFilter::kAll);
    CHECK(q.providerIds.isEmpty());
    CHECK_FALSE(q.isEmpty());
}

TEST_CASE("tree_filters preserves order, counts status and counts tags across mods") {
    const Tag t1{.id = 1, .name = QStringLiteral("Medieval"), .color = QStringLiteral("#5eead4")};
    const Tag t2{.id = 2, .name = QStringLiteral("Combat"), .color = QStringLiteral("#fb923c")};
    const Tag t3{.id = 3, .name = QStringLiteral("Magic"), .color = QStringLiteral("#a3e635")};

    const OrganizerState state = makeState(
        {}, {}, {},
        {{QStringLiteral("a.one"), {1, 2}}, {QStringLiteral("a.two"), {1}}},
        {t1, t2, t3});

    const ModsByUuid mods{
        {QStringLiteral("u1"), makeMod(QStringLiteral("u1"), QStringLiteral("One"), QStringLiteral("a.one"), {}, QStringLiteral("steam"))},
        {QStringLiteral("u2"), makeMod(QStringLiteral("u2"), QStringLiteral("Two"), QStringLiteral("a.two"), {}, QStringLiteral("local"))},
        {QStringLiteral("u3"), makeMod(QStringLiteral("u3"), QStringLiteral("Three"), QStringLiteral("a.three"), {}, QStringLiteral("steam"))},
    };

    const QList<TreeFilter> filters = treeFilters(mods, {QStringLiteral("u1")}, &state);
    REQUIRE(filters.size() >= 3);
    CHECK(filters[0].key == QStringLiteral("all"));
    CHECK(filters[0].count == 3);
    CHECK(filters[1].key == QStringLiteral("active"));
    CHECK(filters[1].count == 1);
    CHECK(filters[2].key == QStringLiteral("inactive"));
    CHECK(filters[2].count == 2);

    QList<TreeFilter> providers;
    for (const TreeFilter& f : filters) {
        if (f.providerId) {
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
        if (f.tagId) {
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
    const OrganizerState state = makeState({}, {}, {}, {{QStringLiteral("shared.mod"), {5}}}, {t5});

    const ModsByUuid mods{
        {QStringLiteral("u1"), makeMod(QStringLiteral("u1"), QStringLiteral("Steam Copy"), QStringLiteral("Shared.Mod"), {}, QStringLiteral("steam"))},
        {QStringLiteral("u2"), makeMod(QStringLiteral("u2"), QStringLiteral("Local Copy"), QStringLiteral("shared.mod"), {}, QStringLiteral("local"))},
        {QStringLiteral("u3"), makeMod(QStringLiteral("u3"), QStringLiteral("Other Mod"), QStringLiteral("other.mod"), {}, QStringLiteral("local"))},
    };

    const QList<TreeFilter> filters = treeFilters(mods, {}, &state);
    const auto it = std::find_if(filters.cbegin(), filters.cend(), [](const TreeFilter& f) { return f.key == QStringLiteral("tag:5"); });
    REQUIRE(it != filters.cend());
    CHECK(it->count == 2);
    CHECK(it->label == QStringLiteral("Overhaul"));
    CHECK(it->tagId == 5);
}

TEST_CASE("tree_filters without state or without tags only produces presets and providers") {
    const ModsByUuid mods{{QStringLiteral("u1"), makeMod(QStringLiteral("u1"), QStringLiteral("One"), QStringLiteral("a.one"))}};

    const QList<TreeFilter> withoutState = treeFilters(mods, {}, nullptr);
    QStringList keys;
    for (const TreeFilter& f : withoutState) {
        keys.append(f.key);
    }
    CHECK(keys == QStringList{QStringLiteral("all"), QStringLiteral("active"), QStringLiteral("inactive"), QStringLiteral("provider:local")});

    const OrganizerState emptyState = makeState();
    const QList<TreeFilter> withEmptyState = treeFilters(mods, {}, &emptyState);
    QStringList emptyKeys;
    for (const TreeFilter& f : withEmptyState) {
        emptyKeys.append(f.key);
    }
    CHECK(emptyKeys == keys);
}

TEST_CASE("filtering tree with a tag query selects only tagged mods") {
    const Tag t1{.id = 1, .name = QStringLiteral("Tagged"), .color = QStringLiteral("#5eead4")};
    const Tag t2{.id = 2, .name = QStringLiteral("Empty"), .color = QStringLiteral("#fb923c")};
    const OrganizerState state = makeState(
        {}, {}, {},
        {{QStringLiteral("a.one"), {1}}, {QStringLiteral("a.two"), {1}}},
        {t1, t2});

    const ModsByUuid mods{
        {QStringLiteral("u1"), makeMod(QStringLiteral("u1"), QStringLiteral("Mod One"), QStringLiteral("a.one"))},
        {QStringLiteral("u2"), makeMod(QStringLiteral("u2"), QStringLiteral("Mod Two"), QStringLiteral("a.two"))},
        {QStringLiteral("u3"), makeMod(QStringLiteral("u3"), QStringLiteral("Mod Three"), QStringLiteral("a.three"))},
    };

    const QList<TreeFilter> filters = treeFilters(mods, {}, &state);
    const auto it1 = std::find_if(filters.cbegin(), filters.cend(), [](const TreeFilter& f) { return f.key == QStringLiteral("tag:1"); });
    REQUIRE(it1 != filters.cend());

    const FolderNode tree1 = buildTree(state, mods, {}, it1->query());
    QSet<QString> visible1;
    for (const ModLeaf& leaf : tree1.allMods()) {
        visible1.insert(leaf.uuid);
    }
    CHECK(visible1 == QSet<QString>{QStringLiteral("u1"), QStringLiteral("u2")});

    const auto it2 = std::find_if(filters.cbegin(), filters.cend(), [](const TreeFilter& f) { return f.key == QStringLiteral("tag:2"); });
    REQUIRE(it2 != filters.cend());
    const FolderNode emptyTree = buildTree(state, mods, {}, it2->query());
    CHECK(emptyTree.allMods().isEmpty());
    CHECK(emptyTree.visibleTotal == 0);

    const FolderNode narrowed = buildTree(state, mods, {}, it1->query(QStringLiteral("Two")));
    QSet<QString> visibleNarrowed;
    for (const ModLeaf& leaf : narrowed.allMods()) {
        visibleNarrowed.insert(leaf.uuid);
    }
    CHECK(visibleNarrowed == QSet<QString>{QStringLiteral("u2")});
}
