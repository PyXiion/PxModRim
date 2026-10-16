#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <variant>

#include <QDir>
#include <QTemporaryDir>

#include "core/models/metadata/structures.hpp"
#include "core/organizer/db.hpp"
#include "core/organizer/defaults.hpp"
#include "core/organizer/models.hpp"
#include "core/organizer/resolve.hpp"
#include "core/organizer/service.hpp"
#include "test_util.hpp"

using namespace px::modrim;
using namespace px::modrim::organizer;
using organizer_test::expect;
using organizer_test::expect_error;
using organizer_test::wait;
using test_support::TestApplication;

namespace {

struct FakeContext {
    QHash<QString, ListedModPtr> all_mods;
    QStringList active_uuids;

    void add(const QString& uuid, const QString& name, const QString& pid,
             const QString& provider = QStringLiteral("local")) {
        auto mod = std::make_shared<AboutXmlMod>();
        mod->name = name;
        mod->provider_id = provider;
        mod->package_id = CaseInsensitiveStr(QStringView(pid));
        all_mods.insert(uuid, mod);
    }
};

struct ChangeCounter {
    int count = 0;
    void record(const std::monostate&) { ++count; }
};

struct TestHarness {
    TestApplication app;
    QTemporaryDir dir;
    FakeContext ctx;
    OrganizerDb db;
    OrganizerService svc;
    ChangeCounter changes;

    explicit TestHarness()
        : dir(),
          db(dir.filePath(QStringLiteral("organizer.db"))),
          svc([this]() -> const auto& { return ctx.all_mods; },
              [this]() -> const auto& { return ctx.active_uuids; }, db) {
        REQUIRE(dir.isValid());
        svc.changed.connect([this](const std::monostate& m) { changes.record(m); });
        wait(svc.init());
        // Reset counter after init notification so tests count mutations only.
        changes.count = 0;
    }

    ~TestHarness() {
        wait(svc.shutdown());
    }
};

}  // namespace

TEST_CASE("bulk folder expansion and collapse persists") {
    TestHarness h;
    const Folder parent = expect(wait(h.svc.create_folder(QStringLiteral("Parent"))));
    const Folder child = expect(wait(h.svc.create_folder(QStringLiteral("Child"), parent.id)));
    const Folder other = expect(wait(h.svc.create_folder(QStringLiteral("Other"))));

    expect(wait(h.svc.set_all_collapsed(true)));
    OrganizerState persisted = expect(wait(h.db.load()));
    CHECK(persisted.folders.value(parent.id).collapsed);
    CHECK(persisted.folders.value(child.id).collapsed);
    CHECK(persisted.folders.value(other.id).collapsed);

    expect(wait(h.svc.set_collapsed(child.id, false)));
    expect(wait(h.svc.set_all_collapsed(false)));
    persisted = expect(wait(h.db.load()));
    CHECK_FALSE(persisted.folders.value(parent.id).collapsed);
    CHECK_FALSE(persisted.folders.value(child.id).collapsed);
    CHECK_FALSE(persisted.folders.value(other.id).collapsed);
}

TEST_CASE("folder depth cannot exceed maximum depth") {
    TestHarness h;
    const Folder a = expect(wait(h.svc.create_folder(QStringLiteral("A"))));
    const Folder b = expect(wait(h.svc.create_folder(QStringLiteral("B"), a.id)));
    const Folder c = expect(wait(h.svc.create_folder(QStringLiteral("C"), b.id)));
    CHECK(h.changes.count == 3);

    expect_error(wait(h.svc.create_folder(QStringLiteral("D"), c.id)), QStringLiteral("at most 3 levels"));
    CHECK(h.changes.count == 3);
    CHECK(h.svc.state().folders.size() == 4);
}

TEST_CASE("moving a folder respects subtree height") {
    TestHarness h;
    const Folder a = expect(wait(h.svc.create_folder(QStringLiteral("A"))));
    const Folder b = expect(wait(h.svc.create_folder(QStringLiteral("B"), a.id)));
    const Folder x = expect(wait(h.svc.create_folder(QStringLiteral("X"))));
    const Folder y = expect(wait(h.svc.create_folder(QStringLiteral("Y"), x.id)));

    expect_error(wait(h.svc.move_folder(x.id, b.id)), QStringLiteral("at most 3 levels"));

    expect(wait(h.svc.move_folder(x.id, a.id)));
    CHECK(h.svc.state().folders.value(x.id).parent_id == a.id);
    CHECK(h.svc.state().folders.value(y.id).parent_id == x.id);
    CHECK(h.changes.count == 5);
}

TEST_CASE("moving a folder into its own subtree or moving root is rejected") {
    TestHarness h;
    const Folder a = expect(wait(h.svc.create_folder(QStringLiteral("A"))));
    const Folder b = expect(wait(h.svc.create_folder(QStringLiteral("B"), a.id)));

    expect_error(wait(h.svc.move_folder(a.id, a.id)), QStringLiteral("into itself"));
    expect_error(wait(h.svc.move_folder(a.id, b.id)), QStringLiteral("into itself"));
    expect_error(wait(h.svc.move_folder(ROOT_ID, a.id)));

    CHECK(h.changes.count == 2);
    CHECK(h.svc.state().folders.value(a.id).parent_id == ROOT_ID);
}

TEST_CASE("folder creation and rename reject duplicate names case-insensitively") {
    TestHarness h;
    const Folder a = expect(wait(h.svc.create_folder(QStringLiteral("Graphics"))));
    expect_error(wait(h.svc.create_folder(QStringLiteral("  graphics "))), QStringLiteral("already exists"));

    const Folder other = expect(wait(h.svc.create_folder(QStringLiteral("Other"))));
    expect_error(wait(h.svc.rename_folder(other.id, QStringLiteral("GRAPHICS"))), QStringLiteral("already exists"));

    const Folder in_other = expect(wait(h.svc.create_folder(QStringLiteral("Graphics"), other.id)));
    CHECK(in_other.parent_id == other.id);

    expect(wait(h.svc.rename_folder(a.id, QStringLiteral("graphics"))));
    CHECK(h.svc.state().folders.value(a.id).name == QStringLiteral("graphics"));
    CHECK(h.changes.count == 4);
}

TEST_CASE("empty folder name is rejected") {
    TestHarness h;
    expect_error(wait(h.svc.create_folder(QStringLiteral("   "))), QStringLiteral("cannot be empty"));
    CHECK(h.changes.count == 0);
}

TEST_CASE("create_folder_from emits once and rolls back on duplicate") {
    TestHarness h;
    const Folder f = expect(wait(h.svc.create_folder_from(
        QStringLiteral("Core"), {QStringLiteral("mod.a"), QStringLiteral("mod.b")})));
    CHECK(h.changes.count == 1);
    CHECK(f.name == QStringLiteral("Core"));
    CHECK(h.svc.state().placements.value(QStringLiteral("mod.a")) == f.id);
    CHECK(h.svc.state().placements.value(QStringLiteral("mod.b")) == f.id);

    const OrganizerState state_before = h.svc.state();
    const int changes_before = h.changes.count;

    expect_error(wait(h.svc.create_folder_from(QStringLiteral("core"), {QStringLiteral("mod.c")})));
    CHECK(h.changes.count == changes_before);
    CHECK(h.svc.state() == state_before);

    const OrganizerState db_state = expect(wait(h.db.load()));
    CHECK_FALSE(db_state.placements.contains(QStringLiteral("mod.c")));
    CHECK(db_state.folders.size() == state_before.folders.size());
}

TEST_CASE("deleting a folder ungroups mods to root manual placement") {
    TestHarness h;
    h.ctx.add(QStringLiteral("u1"), QStringLiteral("One"), QStringLiteral("a.one"));
    h.ctx.add(QStringLiteral("u2"), QStringLiteral("Two"), QStringLiteral("a.two"));
    h.ctx.active_uuids = {QStringLiteral("u1")};

    const Folder a = expect(wait(h.svc.create_folder_from(QStringLiteral("A"), {QStringLiteral("A.One")})));
    const Folder b = expect(wait(h.svc.create_folder(QStringLiteral("B"), a.id)));
    expect(wait(h.svc.place({QStringLiteral("a.two")}, b.id)));
    expect(wait(h.svc.set_rules({RuleSpec{.field = RuleField::PackageId, .op = RuleOp::Prefix,
                                          .pattern = QStringLiteral("a."), .folder_id = a.id}})));
    CHECK(h.changes.count == 4);

    QStringList uuids = expect(h.svc.folder_mod_uuids(a.id));
    std::sort(uuids.begin(), uuids.end());
    CHECK(uuids == QStringList{QStringLiteral("u1"), QStringLiteral("u2")});

    expect(wait(h.svc.delete_folder(a.id)));
    CHECK(h.changes.count == 5);
    CHECK(h.svc.state().folders.keys() == QList<qint64>{ROOT_ID});
    CHECK(h.svc.state().rules.isEmpty());

    const FolderNode root = h.svc.tree();
    QStringList root_uuids;
    for (const ModLeaf& leaf : root.mods) {
        root_uuids.append(leaf.uuid);
        CHECK(leaf.placement == Placement::Manual);
    }
    std::sort(root_uuids.begin(), root_uuids.end());
    CHECK(root_uuids == QStringList{QStringLiteral("u1"), QStringLiteral("u2")});
    CHECK(root.total == 2);
    CHECK(root.enabled == 1);
    CHECK(root.check == CheckState::Partial);
}

TEST_CASE("place, ungroup and reset_to_rules alter placement source") {
    TestHarness h;
    h.ctx.add(QStringLiteral("u1"), QStringLiteral("One"), QStringLiteral("a.one"));
    const Folder a = expect(wait(h.svc.create_folder(QStringLiteral("A"))));
    expect(wait(h.svc.set_rules({RuleSpec{.field = RuleField::Name, .op = RuleOp::Equals,
                                          .pattern = QStringLiteral(" one "), .folder_id = a.id}})));
    CHECK(h.svc.state().rules[0].pattern == QStringLiteral("one"));
    CHECK(h.svc.tree().children[0].mods[0].placement == Placement::Rule);

    expect(wait(h.svc.ungroup({QStringLiteral("a.one")})));
    CHECK(h.svc.tree().mods[0].placement == Placement::Manual);

    expect(wait(h.svc.reset_to_rules({QStringLiteral("a.one")})));
    CHECK(h.svc.tree().children[0].mods[0].placement == Placement::Rule);
    CHECK(h.changes.count == 4);
}

TEST_CASE("rules validation rejects root target, empty pattern or missing folder") {
    TestHarness h;
    const Folder a = expect(wait(h.svc.create_folder(QStringLiteral("A"))));

    expect_error(wait(h.svc.set_rules({RuleSpec{.field = RuleField::Name, .op = RuleOp::Equals,
                                                .pattern = QStringLiteral("x"), .folder_id = ROOT_ID}})),
                 QStringLiteral("root"));

    expect_error(wait(h.svc.set_rules({RuleSpec{.field = RuleField::Name, .op = RuleOp::Equals,
                                                .pattern = QStringLiteral("  "), .folder_id = a.id}})),
                 QStringLiteral("pattern cannot be empty"));

    expect_error(wait(h.svc.set_rules({RuleSpec{.field = RuleField::Name, .op = RuleOp::Equals,
                                                .pattern = QStringLiteral("x"), .folder_id = 999}})));
    CHECK(h.changes.count == 1);
}

TEST_CASE("tags lifecycle, validation and removal cascade") {
    TestHarness h;
    const Tag tag = expect(wait(h.svc.create_tag(QStringLiteral("QoL"), QStringLiteral("#aabbcc"))));

    expect_error(wait(h.svc.create_tag(QStringLiteral("qol"), QStringLiteral("#000000"))), QStringLiteral("already exists"));
    expect_error(wait(h.svc.create_tag(QStringLiteral("Other"), QStringLiteral("red"))), QStringLiteral("not a valid color"));

    expect(wait(h.svc.set_mod_tags({QStringLiteral("A.Mod")}, {tag.id}, {})));
    CHECK(h.svc.state().mod_tags.value(QStringLiteral("a.mod")) == QSet<qint64>{tag.id});

    expect_error(wait(h.svc.set_mod_tags({QStringLiteral("a.mod")}, {999}, {})));

    expect(wait(h.svc.delete_tag(tag.id)));
    CHECK(h.svc.state().tags.isEmpty());
    CHECK(h.svc.state().mod_tags.value(QStringLiteral("a.mod")).isEmpty());
    CHECK(h.changes.count == 3);
}

TEST_CASE("service state persists across instances") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString db_file = dir.filePath(QStringLiteral("organizer.db"));
    FakeContext ctx;

    Folder folder;
    {
        OrganizerDb db1(db_file);
        OrganizerService svc1([&]() -> const auto& { return ctx.all_mods; },
                              [&]() -> const auto& { return ctx.active_uuids; }, db1);
        wait(svc1.init());
        folder = expect(wait(svc1.create_folder(QStringLiteral("Kept"))));
        wait(svc1.shutdown());
    }

    {
        OrganizerDb db2(db_file);
        OrganizerService svc2([&]() -> const auto& { return ctx.all_mods; },
                              [&]() -> const auto& { return ctx.active_uuids; }, db2);
        wait(svc2.init());
        CHECK(svc2.state().folders.contains(folder.id));
        CHECK(svc2.state().folders.value(folder.id).name == QStringLiteral("Kept"));
        wait(svc2.shutdown());
    }
}

TEST_CASE("init signals readiness and emits changed") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    FakeContext ctx;
    OrganizerDb db(dir.filePath(QStringLiteral("organizer.db")));
    OrganizerService svc([&]() -> const auto& { return ctx.all_mods; },
                         [&]() -> const auto& { return ctx.active_uuids; }, db);
    ChangeCounter counter;
    svc.changed.connect([&](const std::monostate& m) { counter.record(m); });

    CHECK_FALSE(svc.ready());
    wait(svc.init());
    CHECK(svc.ready());
    CHECK(counter.count == 1);
    wait(svc.shutdown());
}

TEST_CASE("folder_toggle enables partial/off and disables full") {
    TestHarness h;
    h.ctx.add(QStringLiteral("u1"), QStringLiteral("One"), QStringLiteral("a.one"));
    h.ctx.add(QStringLiteral("u2"), QStringLiteral("Two"), QStringLiteral("a.two"));
    h.ctx.add(QStringLiteral("u3"), QStringLiteral("Loose"), QStringLiteral("loose.mod"));

    const Folder a = expect(wait(h.svc.create_folder_from(QStringLiteral("A"), {QStringLiteral("a.one")})));
    const Folder b = expect(wait(h.svc.create_folder(QStringLiteral("B"), a.id)));
    expect(wait(h.svc.place({QStringLiteral("a.two")}, b.id)));

    h.ctx.active_uuids = {QStringLiteral("u1")};
    FolderToggle toggle = expect(h.svc.folder_toggle(a.id));
    toggle.enable.sort();
    CHECK(toggle.enable == QStringList{QStringLiteral("u1"), QStringLiteral("u2")});
    CHECK(toggle.disable.isEmpty());

    h.ctx.active_uuids = {QStringLiteral("u1"), QStringLiteral("u2")};
    toggle = expect(h.svc.folder_toggle(a.id));
    toggle.disable.sort();
    CHECK(toggle.enable.isEmpty());
    CHECK(toggle.disable == QStringList{QStringLiteral("u1"), QStringLiteral("u2")});

    toggle = expect(h.svc.folder_toggle(ROOT_ID, /*recursive=*/false));
    CHECK(toggle.enable == QStringList{QStringLiteral("u3")});
    CHECK(toggle.disable.isEmpty());

    h.ctx.active_uuids = {QStringLiteral("u3")};
    toggle = expect(h.svc.folder_toggle(ROOT_ID, /*recursive=*/false));
    CHECK(toggle.enable.isEmpty());
    CHECK(toggle.disable == QStringList{QStringLiteral("u3")});

    CHECK(expect(h.svc.folder_mod_uuids(ROOT_ID, /*recursive=*/false)) == QStringList{QStringLiteral("u3")});
}

TEST_CASE("folder_move_targets respects subtree depth and duplicate names") {
    TestHarness h;
    const Folder a = expect(wait(h.svc.create_folder(QStringLiteral("A"))));
    const Folder b = expect(wait(h.svc.create_folder(QStringLiteral("B"), a.id)));
    const Folder c = expect(wait(h.svc.create_folder(QStringLiteral("C"), b.id)));
    const Folder x = expect(wait(h.svc.create_folder(QStringLiteral("X"))));
    const Folder y = expect(wait(h.svc.create_folder(QStringLiteral("Y"), x.id)));
    const Folder clash = expect(wait(h.svc.create_folder(QStringLiteral("Clash"))));
    const Folder inner_x = expect(wait(h.svc.create_folder(QStringLiteral("X"), clash.id)));

    CHECK(expect(h.svc.folder_move_targets(b.id)) == QSet<qint64>{x.id, clash.id, ROOT_ID});
    CHECK(expect(h.svc.folder_move_targets(x.id)) == QSet<qint64>{a.id});
    CHECK(expect(h.svc.folder_move_targets(c.id)) == QSet<qint64>{a.id, x.id, y.id, clash.id, inner_x.id, ROOT_ID});
    expect_error(h.svc.folder_move_targets(ROOT_ID));
}

TEST_CASE("service tree_filters counts status and providers") {
    TestHarness h;
    h.ctx.add(QStringLiteral("u1"), QStringLiteral("One"), QStringLiteral("a.one"));
    h.ctx.add(QStringLiteral("u2"), QStringLiteral("Two"), QStringLiteral("a.two"));
    h.ctx.active_uuids = {QStringLiteral("u1")};

    QHash<QString, int> counts;
    for (const TreeFilter& f : h.svc.tree_filters()) {
        counts[f.key] = f.count;
    }
    CHECK(counts.value(QStringLiteral("all")) == 2);
    CHECK(counts.value(QStringLiteral("active")) == 1);
    CHECK(counts.value(QStringLiteral("inactive")) == 1);
    CHECK(counts.value(QStringLiteral("provider:local")) == 2);
}

TEST_CASE("service tree_filters with persisted tags and empty tag") {
    TestHarness h;
    h.ctx.add(QStringLiteral("u1"), QStringLiteral("One"), QStringLiteral("a.one"));
    h.ctx.add(QStringLiteral("u2"), QStringLiteral("Two"), QStringLiteral("a.two"));
    h.ctx.add(QStringLiteral("u3"), QStringLiteral("Three"), QStringLiteral("a.three"));

    const Tag t1 = expect(wait(h.svc.create_tag(QStringLiteral("Medieval"), QStringLiteral("#5eead4"))));
    const Tag t2 = expect(wait(h.svc.create_tag(QStringLiteral("Combat"), QStringLiteral("#fb923c"))));
    const Tag t3 = expect(wait(h.svc.create_tag(QStringLiteral("Empty"), QStringLiteral("#a3e635"))));

    expect(wait(h.svc.set_mod_tags({QStringLiteral("a.one")}, {t1.id, t2.id}, {})));
    expect(wait(h.svc.set_mod_tags({QStringLiteral("a.two")}, {t1.id}, {})));

    QHash<QString, TreeFilter> filter_map;
    for (const TreeFilter& f : h.svc.tree_filters()) {
        filter_map[f.key] = f;
    }

    const QString k1 = QStringLiteral("tag:%1").arg(t1.id);
    const QString k2 = QStringLiteral("tag:%1").arg(t2.id);
    const QString k3 = QStringLiteral("tag:%1").arg(t3.id);

    REQUIRE(filter_map.contains(k1));
    CHECK(filter_map[k1].count == 2);
    CHECK(filter_map[k1].label == QStringLiteral("Medieval"));

    REQUIRE(filter_map.contains(k2));
    CHECK(filter_map[k2].count == 1);
    CHECK(filter_map[k2].label == QStringLiteral("Combat"));

    REQUIRE(filter_map.contains(k3));
    CHECK(filter_map[k3].count == 0);
    CHECK(filter_map[k3].label == QStringLiteral("Empty"));

    expect(wait(h.svc.delete_tag(t2.id)));
    filter_map.clear();
    for (const TreeFilter& f : h.svc.tree_filters()) {
        filter_map[f.key] = f;
    }
    CHECK_FALSE(filter_map.contains(k2));
    CHECK(filter_map.contains(k1));
}

TEST_CASE("service tree_filters counts shared package ids across copies") {
    TestHarness h;
    h.ctx.add(QStringLiteral("u1"), QStringLiteral("Steam Copy"), QStringLiteral("shared.pkg"), QStringLiteral("steam"));
    h.ctx.add(QStringLiteral("u2"), QStringLiteral("Local Copy"), QStringLiteral("shared.pkg"), QStringLiteral("local"));
    h.ctx.add(QStringLiteral("u3"), QStringLiteral("Other Mod"), QStringLiteral("other.pkg"), QStringLiteral("local"));

    const Tag tag = expect(wait(h.svc.create_tag(QStringLiteral("Overhaul"), QStringLiteral("#5eead4"))));
    expect(wait(h.svc.set_mod_tags({QStringLiteral("shared.pkg")}, {tag.id}, {})));

    QHash<QString, int> counts;
    for (const TreeFilter& f : h.svc.tree_filters()) {
        counts[f.key] = f.count;
    }
    CHECK(counts.value(QStringLiteral("tag:%1").arg(tag.id)) == 2);
}

TEST_CASE("add_standard_rules is fresh and idempotent") {
    TestHarness h;
    const int total = static_cast<int>(standard_rules().size());
    QSet<QString> names;
    for (const StandardRule& r : standard_rules()) {
        names.insert(r.folder);
    }

    CHECK(expect(wait(h.svc.add_standard_rules())) == total);
    CHECK(h.changes.count == 1);

    QSet<QString> top_folders;
    for (const Folder& f : h.svc.state().folders) {
        if (f.parent_id == ROOT_ID) {
            top_folders.insert(f.name);
        }
    }
    CHECK(top_folders == names);
    CHECK(h.svc.state().folders.size() == names.size() + 1);
    CHECK(h.svc.state().rules.size() == total);

    CHECK(expect(wait(h.svc.add_standard_rules())) == 0);
    CHECK(h.changes.count == 1);
    CHECK(h.svc.state().rules.size() == total);
    CHECK(h.svc.state().folders.size() == names.size() + 1);
}

TEST_CASE("add_standard_rules reuses existing folders and preserves user rules") {
    TestHarness h;
    const Folder official = expect(wait(h.svc.create_folder(QStringLiteral("OFFICIAL"))));
    const Folder mine = expect(wait(h.svc.create_folder(QStringLiteral("Mine"))));
    expect(wait(h.svc.set_rules({
        RuleSpec{.field = RuleField::Author, .op = RuleOp::Contains, .pattern = QStringLiteral("me"), .folder_id = mine.id},
        RuleSpec{.field = RuleField::PackageId, .op = RuleOp::Prefix, .pattern = QStringLiteral("SARG."), .folder_id = mine.id},
    })));
    h.changes.count = 0;

    const int total = static_cast<int>(standard_rules().size());
    CHECK(expect(wait(h.svc.add_standard_rules())) == total - 1);
    CHECK(h.changes.count == 1);

    QStringList names;
    for (const Folder& f : h.svc.state().folders) {
        names.append(f.name);
    }
    CHECK_FALSE(names.contains(QStringLiteral("Official")));
    CHECK_FALSE(names.contains(QStringLiteral("Alpha Mods")));
    CHECK(names.count(QStringLiteral("OFFICIAL")) == 1);

    const auto& rules = h.svc.state().rules;
    REQUIRE(rules.size() >= 3);
    CHECK(h.svc.state().folders.value(rules[0].folder_id).name == QStringLiteral("Mine"));
    CHECK(rules[0].pattern == QStringLiteral("me"));
    CHECK(h.svc.state().folders.value(rules[1].folder_id).name == QStringLiteral("Mine"));
    CHECK(rules[1].pattern == QStringLiteral("SARG."));

    // The first standard rule for Official got re-targeted to the existing OFFICIAL folder.
    CHECK(rules[2].folder_id == official.id);
}

TEST_CASE("standard rules resolve mods correctly") {
    TestHarness h;
    expect(wait(h.svc.add_standard_rules()));
    const OrganizerState& state = h.svc.state();

    const auto folder_name = [&](const QString& pid, const QString& name = QStringLiteral("Mod"),
                                 const QString& author = {}) {
        const auto [fid, _] = folder_for(state, pid, name, author);
        return state.folders.value(fid).name;
    };

    CHECK(folder_name(QStringLiteral("oskarpotocki.vfe.core")) == QStringLiteral("Vanilla Expanded"));
    CHECK(folder_name(QStringLiteral("someone.vfe"), QStringLiteral("Mod"), QStringLiteral("Oskar Potocki, Taranchuk")) ==
          QStringLiteral("Vanilla Expanded"));
    CHECK(folder_name(QStringLiteral("ludeon.rimworld.biotech"), QStringLiteral("Biotech")) == QStringLiteral("Official"));
    CHECK(folder_name(QStringLiteral("brrainz.harmony"), QStringLiteral("Harmony")) == QStringLiteral("Frameworks & Libraries"));
    CHECK(folder_name(QStringLiteral("ceteam.combatextended"), QStringLiteral("Combat Extended")) == QStringLiteral("Combat Extended"));
    CHECK(folder_name(QStringLiteral("sarg.alphagenes")) == QStringLiteral("Alpha Mods"));
    CHECK(folder_name(QStringLiteral("bs.performance"), QStringLiteral("Performance Fish")) == QStringLiteral("Performance"));
    CHECK(folder_name(QStringLiteral("oskarpotocki.vanillafactionsexpanded.core")) == QStringLiteral("Frameworks & Libraries"));
    CHECK(folder_name(QStringLiteral("vanillaracesexpanded.sanguophage")) == QStringLiteral("Vanilla Expanded"));
    CHECK(folder_name(QStringLiteral("dubwise.dubsperformanceanalyzer.steam")) == QStringLiteral("Performance"));
    CHECK(folder_name(QStringLiteral("dubwise.rimatomics")) == QStringLiteral("Dubs Mods"));
    CHECK(folder_name(QStringLiteral("brrainz.achtung"), QStringLiteral("Achtung!")) == QStringLiteral("Quality of Life"));
    CHECK(folder_name(QStringLiteral("brrainz.jobsofopportunity")) == QStringLiteral("Root"));
    CHECK(folder_name(QStringLiteral("smartkar.athena.framework"), QStringLiteral("Athena Framework")) == QStringLiteral("Root"));
    CHECK(folder_name(QStringLiteral("someone.perf"), QStringLiteral("Performance Tweaks")) == QStringLiteral("Root"));
    CHECK(folder_name(QStringLiteral("someone.other"), QStringLiteral("Other")) == QStringLiteral("Root"));
}

TEST_CASE("standard package_id rules are not shadowed") {
    TestHarness h;
    expect(wait(h.svc.add_standard_rules()));
    const OrganizerState& state = h.svc.state();

    for (const StandardRule& rule : standard_rules()) {
        if (rule.field != RuleField::PackageId) {
            continue;
        }
        const QString probe = rule.op == RuleOp::Equals ? rule.pattern : rule.pattern + QStringLiteral("x");
        const auto [fid, _] = folder_for(state, probe, QStringLiteral("Mod"), {});
        CHECK(state.folders.value(fid).name == rule.folder);
    }
}
