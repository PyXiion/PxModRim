#include <QDir>
#include <QTemporaryDir>
#include <memory>
#include <variant>

#include <catch2/catch_test_macros.hpp>

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
using organizer_test::expectError;
using organizer_test::wait;
using test_support::TestApplication;

namespace {

struct FakeContext {
  QHash<QString, ListedModPtr> allMods;
  QStringList activeUuids;

  void add(const QString& uuid, const QString& name, const QString& pid,
           const QString& provider = QStringLiteral("local")) {
    auto mod = std::make_shared<AboutXmlMod>();
    mod->name = name;
    mod->providerId = provider;
    mod->packageId = CaseInsensitiveStr(QStringView(pid));
    allMods.insert(uuid, mod);
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
        svc([this]() -> const auto& { return ctx.allMods; },
            [this]() -> const auto& { return ctx.activeUuids; }, db) {
    REQUIRE(dir.isValid());
    svc.changed.connect([this](const std::monostate& m) { changes.record(m); });
    wait(svc.init());
    // Reset counter after init notification so tests count mutations only.
    changes.count = 0;
  }

  ~TestHarness() { wait(svc.shutdown()); }
};

}  // namespace

TEST_CASE("bulk folder expansion and collapse persists") {
  TestHarness h;
  const Folder parent =
      expect(wait(h.svc.createFolder(QStringLiteral("Parent"))));
  const Folder child =
      expect(wait(h.svc.createFolder(QStringLiteral("Child"), parent.id)));
  const Folder other =
      expect(wait(h.svc.createFolder(QStringLiteral("Other"))));

  expect(wait(h.svc.setAllCollapsed(true)));
  OrganizerState persisted = expect(wait(h.db.load()));
  CHECK(persisted.folders.value(parent.id).collapsed);
  CHECK(persisted.folders.value(child.id).collapsed);
  CHECK(persisted.folders.value(other.id).collapsed);

  expect(wait(h.svc.setCollapsed(child.id, false)));
  expect(wait(h.svc.setAllCollapsed(false)));
  persisted = expect(wait(h.db.load()));
  CHECK_FALSE(persisted.folders.value(parent.id).collapsed);
  CHECK_FALSE(persisted.folders.value(child.id).collapsed);
  CHECK_FALSE(persisted.folders.value(other.id).collapsed);
}

TEST_CASE("folder depth cannot exceed maximum depth") {
  TestHarness h;
  const Folder a = expect(wait(h.svc.createFolder(QStringLiteral("A"))));
  const Folder b = expect(wait(h.svc.createFolder(QStringLiteral("B"), a.id)));
  const Folder c = expect(wait(h.svc.createFolder(QStringLiteral("C"), b.id)));
  CHECK(h.changes.count == 3);

  expectError(wait(h.svc.createFolder(QStringLiteral("D"), c.id)),
              QStringLiteral("at most 3 levels"));
  CHECK(h.changes.count == 3);
  CHECK(h.svc.state().folders.size() == 4);
}

TEST_CASE("moving a folder respects subtree height") {
  TestHarness h;
  const Folder a = expect(wait(h.svc.createFolder(QStringLiteral("A"))));
  const Folder b = expect(wait(h.svc.createFolder(QStringLiteral("B"), a.id)));
  const Folder x = expect(wait(h.svc.createFolder(QStringLiteral("X"))));
  const Folder y = expect(wait(h.svc.createFolder(QStringLiteral("Y"), x.id)));

  expectError(wait(h.svc.moveFolder(x.id, b.id)),
              QStringLiteral("at most 3 levels"));

  expect(wait(h.svc.moveFolder(x.id, a.id)));
  CHECK(h.svc.state().folders.value(x.id).parentId == a.id);
  CHECK(h.svc.state().folders.value(y.id).parentId == x.id);
  CHECK(h.changes.count == 5);
}

TEST_CASE("moving a folder into its own subtree or moving root is rejected") {
  TestHarness h;
  const Folder a = expect(wait(h.svc.createFolder(QStringLiteral("A"))));
  const Folder b = expect(wait(h.svc.createFolder(QStringLiteral("B"), a.id)));

  expectError(wait(h.svc.moveFolder(a.id, a.id)),
              QStringLiteral("into itself"));
  expectError(wait(h.svc.moveFolder(a.id, b.id)),
              QStringLiteral("into itself"));
  expectError(wait(h.svc.moveFolder(kRootId, a.id)));

  CHECK(h.changes.count == 2);
  CHECK(h.svc.state().folders.value(a.id).parentId == kRootId);
}

TEST_CASE(
    "folder creation and rename reject duplicate names case-insensitively") {
  TestHarness h;
  const Folder a = expect(wait(h.svc.createFolder(QStringLiteral("Graphics"))));
  expectError(wait(h.svc.createFolder(QStringLiteral("  graphics "))),
              QStringLiteral("already exists"));

  const Folder other =
      expect(wait(h.svc.createFolder(QStringLiteral("Other"))));
  expectError(wait(h.svc.renameFolder(other.id, QStringLiteral("GRAPHICS"))),
              QStringLiteral("already exists"));

  const Folder inOther =
      expect(wait(h.svc.createFolder(QStringLiteral("Graphics"), other.id)));
  CHECK(inOther.parentId == other.id);

  expect(wait(h.svc.renameFolder(a.id, QStringLiteral("graphics"))));
  CHECK(h.svc.state().folders.value(a.id).name == QStringLiteral("graphics"));
  CHECK(h.changes.count == 4);
}

TEST_CASE("empty folder name is rejected") {
  TestHarness h;
  expectError(wait(h.svc.createFolder(QStringLiteral("   "))),
              QStringLiteral("cannot be empty"));
  CHECK(h.changes.count == 0);
}

TEST_CASE("create_folder_from emits once and rolls back on duplicate") {
  TestHarness h;
  const Folder f = expect(wait(h.svc.createFolderFrom(
      QStringLiteral("Core"),
      {QStringLiteral("mod.a"), QStringLiteral("mod.b")})));
  CHECK(h.changes.count == 1);
  CHECK(f.name == QStringLiteral("Core"));
  CHECK(h.svc.state().placements.value(QStringLiteral("mod.a")) == f.id);
  CHECK(h.svc.state().placements.value(QStringLiteral("mod.b")) == f.id);

  const OrganizerState stateBefore = h.svc.state();
  const int changesBefore = h.changes.count;

  expectError(wait(h.svc.createFolderFrom(QStringLiteral("core"),
                                          {QStringLiteral("mod.c")})));
  CHECK(h.changes.count == changesBefore);
  CHECK(h.svc.state() == stateBefore);

  const OrganizerState dbState = expect(wait(h.db.load()));
  CHECK_FALSE(dbState.placements.contains(QStringLiteral("mod.c")));
  CHECK(dbState.folders.size() == stateBefore.folders.size());
}

TEST_CASE("deleting a folder ungroups mods to root manual placement") {
  TestHarness h;
  h.ctx.add(QStringLiteral("u1"), QStringLiteral("One"),
            QStringLiteral("a.one"));
  h.ctx.add(QStringLiteral("u2"), QStringLiteral("Two"),
            QStringLiteral("a.two"));
  h.ctx.activeUuids = {QStringLiteral("u1")};

  const Folder a = expect(wait(
      h.svc.createFolderFrom(QStringLiteral("A"), {QStringLiteral("A.One")})));
  const Folder b = expect(wait(h.svc.createFolder(QStringLiteral("B"), a.id)));
  expect(wait(h.svc.place({QStringLiteral("a.two")}, b.id)));
  expect(wait(h.svc.setRules({RuleSpec{.field = RuleField::kPackageId,
                                       .op = RuleOp::kPrefix,
                                       .pattern = QStringLiteral("a."),
                                       .folderId = a.id}})));
  CHECK(h.changes.count == 4);

  QStringList uuids = expect(h.svc.folderModUuids(a.id));
  std::sort(uuids.begin(), uuids.end());
  CHECK(uuids == QStringList{QStringLiteral("u1"), QStringLiteral("u2")});

  expect(wait(h.svc.deleteFolder(a.id)));
  CHECK(h.changes.count == 5);
  CHECK(h.svc.state().folders.keys() == QList<qint64>{kRootId});
  CHECK(h.svc.state().rules.isEmpty());

  const FolderNode root = h.svc.tree();
  QStringList rootUuids;
  for (const ModLeaf& leaf : root.mods) {
    rootUuids.append(leaf.uuid);
    CHECK(leaf.placement == Placement::kManual);
  }
  std::sort(rootUuids.begin(), rootUuids.end());
  CHECK(rootUuids == QStringList{QStringLiteral("u1"), QStringLiteral("u2")});
  CHECK(root.total == 2);
  CHECK(root.enabled == 1);
  CHECK(root.check == CheckState::kPartial);
}

TEST_CASE("place, ungroup and reset_to_rules alter placement source") {
  TestHarness h;
  h.ctx.add(QStringLiteral("u1"), QStringLiteral("One"),
            QStringLiteral("a.one"));
  const Folder a = expect(wait(h.svc.createFolder(QStringLiteral("A"))));
  expect(wait(h.svc.setRules({RuleSpec{.field = RuleField::kName,
                                       .op = RuleOp::kEquals,
                                       .pattern = QStringLiteral(" one "),
                                       .folderId = a.id}})));
  CHECK(h.svc.state().rules[0].pattern == QStringLiteral("one"));
  CHECK(h.svc.tree().children[0].mods[0].placement == Placement::kRule);

  expect(wait(h.svc.ungroup({QStringLiteral("a.one")})));
  CHECK(h.svc.tree().mods[0].placement == Placement::kManual);

  expect(wait(h.svc.resetToRules({QStringLiteral("a.one")})));
  CHECK(h.svc.tree().children[0].mods[0].placement == Placement::kRule);
  CHECK(h.changes.count == 4);
}

TEST_CASE(
    "rules validation rejects root target, empty pattern or missing folder") {
  TestHarness h;
  const Folder a = expect(wait(h.svc.createFolder(QStringLiteral("A"))));

  expectError(wait(h.svc.setRules({RuleSpec{.field = RuleField::kName,
                                            .op = RuleOp::kEquals,
                                            .pattern = QStringLiteral("x"),
                                            .folderId = kRootId}})),
              QStringLiteral("root"));

  expectError(wait(h.svc.setRules({RuleSpec{.field = RuleField::kName,
                                            .op = RuleOp::kEquals,
                                            .pattern = QStringLiteral("  "),
                                            .folderId = a.id}})),
              QStringLiteral("pattern cannot be empty"));

  expectError(wait(h.svc.setRules({RuleSpec{.field = RuleField::kName,
                                            .op = RuleOp::kEquals,
                                            .pattern = QStringLiteral("x"),
                                            .folderId = 999}})));
  CHECK(h.changes.count == 1);
}

TEST_CASE("tags lifecycle, validation and removal cascade") {
  TestHarness h;
  const Tag tag = expect(
      wait(h.svc.createTag(QStringLiteral("QoL"), QStringLiteral("#aabbcc"))));

  expectError(
      wait(h.svc.createTag(QStringLiteral("qol"), QStringLiteral("#000000"))),
      QStringLiteral("already exists"));
  expectError(
      wait(h.svc.createTag(QStringLiteral("Other"), QStringLiteral("red"))),
      QStringLiteral("not a valid color"));

  expect(wait(h.svc.setModTags({QStringLiteral("A.Mod")}, {tag.id}, {})));
  CHECK(h.svc.state().modTags.value(QStringLiteral("a.mod")) ==
        QSet<qint64>{tag.id});

  expectError(wait(h.svc.setModTags({QStringLiteral("a.mod")}, {999}, {})));

  expect(wait(h.svc.deleteTag(tag.id)));
  CHECK(h.svc.state().tags.isEmpty());
  CHECK(h.svc.state().modTags.value(QStringLiteral("a.mod")).isEmpty());
  CHECK(h.changes.count == 3);
}

TEST_CASE("service state persists across instances") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString dbFile = dir.filePath(QStringLiteral("organizer.db"));
  FakeContext ctx;

  Folder folder;
  {
    OrganizerDb db1(dbFile);
    OrganizerService svc1([&]() -> const auto& { return ctx.allMods; },
                          [&]() -> const auto& { return ctx.activeUuids; },
                          db1);
    wait(svc1.init());
    folder = expect(wait(svc1.createFolder(QStringLiteral("Kept"))));
    wait(svc1.shutdown());
  }

  {
    OrganizerDb db2(dbFile);
    OrganizerService svc2([&]() -> const auto& { return ctx.allMods; },
                          [&]() -> const auto& { return ctx.activeUuids; },
                          db2);
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
  OrganizerService svc([&]() -> const auto& { return ctx.allMods; },
                       [&]() -> const auto& { return ctx.activeUuids; }, db);
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
  h.ctx.add(QStringLiteral("u1"), QStringLiteral("One"),
            QStringLiteral("a.one"));
  h.ctx.add(QStringLiteral("u2"), QStringLiteral("Two"),
            QStringLiteral("a.two"));
  h.ctx.add(QStringLiteral("u3"), QStringLiteral("Loose"),
            QStringLiteral("loose.mod"));

  const Folder a = expect(wait(
      h.svc.createFolderFrom(QStringLiteral("A"), {QStringLiteral("a.one")})));
  const Folder b = expect(wait(h.svc.createFolder(QStringLiteral("B"), a.id)));
  expect(wait(h.svc.place({QStringLiteral("a.two")}, b.id)));

  h.ctx.activeUuids = {QStringLiteral("u1")};
  FolderToggle toggle = expect(h.svc.folderToggle(a.id));
  toggle.enable.sort();
  CHECK(toggle.enable ==
        QStringList{QStringLiteral("u1"), QStringLiteral("u2")});
  CHECK(toggle.disable.isEmpty());

  h.ctx.activeUuids = {QStringLiteral("u1"), QStringLiteral("u2")};
  toggle = expect(h.svc.folderToggle(a.id));
  toggle.disable.sort();
  CHECK(toggle.enable.isEmpty());
  CHECK(toggle.disable ==
        QStringList{QStringLiteral("u1"), QStringLiteral("u2")});

  toggle = expect(h.svc.folderToggle(kRootId, /*recursive=*/false));
  CHECK(toggle.enable == QStringList{QStringLiteral("u3")});
  CHECK(toggle.disable.isEmpty());

  h.ctx.activeUuids = {QStringLiteral("u3")};
  toggle = expect(h.svc.folderToggle(kRootId, /*recursive=*/false));
  CHECK(toggle.enable.isEmpty());
  CHECK(toggle.disable == QStringList{QStringLiteral("u3")});

  CHECK(expect(h.svc.folderModUuids(kRootId, /*recursive=*/false)) ==
        QStringList{QStringLiteral("u3")});
}

TEST_CASE("folder_move_targets respects subtree depth and duplicate names") {
  TestHarness h;
  const Folder a = expect(wait(h.svc.createFolder(QStringLiteral("A"))));
  const Folder b = expect(wait(h.svc.createFolder(QStringLiteral("B"), a.id)));
  const Folder c = expect(wait(h.svc.createFolder(QStringLiteral("C"), b.id)));
  const Folder x = expect(wait(h.svc.createFolder(QStringLiteral("X"))));
  const Folder y = expect(wait(h.svc.createFolder(QStringLiteral("Y"), x.id)));
  const Folder clash =
      expect(wait(h.svc.createFolder(QStringLiteral("Clash"))));
  const Folder innerX =
      expect(wait(h.svc.createFolder(QStringLiteral("X"), clash.id)));

  CHECK(expect(h.svc.folderMoveTargets(b.id)) ==
        QSet<qint64>{x.id, clash.id, kRootId});
  CHECK(expect(h.svc.folderMoveTargets(x.id)) == QSet<qint64>{a.id});
  CHECK(expect(h.svc.folderMoveTargets(c.id)) ==
        QSet<qint64>{a.id, x.id, y.id, clash.id, innerX.id, kRootId});
  expectError(h.svc.folderMoveTargets(kRootId));
}

TEST_CASE("service tree_filters counts status and providers") {
  TestHarness h;
  h.ctx.add(QStringLiteral("u1"), QStringLiteral("One"),
            QStringLiteral("a.one"));
  h.ctx.add(QStringLiteral("u2"), QStringLiteral("Two"),
            QStringLiteral("a.two"));
  h.ctx.activeUuids = {QStringLiteral("u1")};

  QHash<QString, int> counts;
  for (const TreeFilter& f : h.svc.treeFilters()) {
    counts[f.key] = f.count;
  }
  CHECK(counts.value(QStringLiteral("all")) == 2);
  CHECK(counts.value(QStringLiteral("active")) == 1);
  CHECK(counts.value(QStringLiteral("inactive")) == 1);
  CHECK(counts.value(QStringLiteral("provider:local")) == 2);
}

TEST_CASE("service tree_filters with persisted tags and empty tag") {
  TestHarness h;
  h.ctx.add(QStringLiteral("u1"), QStringLiteral("One"),
            QStringLiteral("a.one"));
  h.ctx.add(QStringLiteral("u2"), QStringLiteral("Two"),
            QStringLiteral("a.two"));
  h.ctx.add(QStringLiteral("u3"), QStringLiteral("Three"),
            QStringLiteral("a.three"));

  const Tag t1 = expect(wait(
      h.svc.createTag(QStringLiteral("Medieval"), QStringLiteral("#5eead4"))));
  const Tag t2 = expect(wait(
      h.svc.createTag(QStringLiteral("Combat"), QStringLiteral("#fb923c"))));
  const Tag t3 = expect(wait(
      h.svc.createTag(QStringLiteral("Empty"), QStringLiteral("#a3e635"))));

  expect(wait(h.svc.setModTags({QStringLiteral("a.one")}, {t1.id, t2.id}, {})));
  expect(wait(h.svc.setModTags({QStringLiteral("a.two")}, {t1.id}, {})));

  QHash<QString, TreeFilter> filterMap;
  for (const TreeFilter& f : h.svc.treeFilters()) {
    filterMap[f.key] = f;
  }

  const QString k1 = QStringLiteral("tag:%1").arg(t1.id);
  const QString k2 = QStringLiteral("tag:%1").arg(t2.id);
  const QString k3 = QStringLiteral("tag:%1").arg(t3.id);

  REQUIRE(filterMap.contains(k1));
  CHECK(filterMap[k1].count == 2);
  CHECK(filterMap[k1].label == QStringLiteral("Medieval"));

  REQUIRE(filterMap.contains(k2));
  CHECK(filterMap[k2].count == 1);
  CHECK(filterMap[k2].label == QStringLiteral("Combat"));

  REQUIRE(filterMap.contains(k3));
  CHECK(filterMap[k3].count == 0);
  CHECK(filterMap[k3].label == QStringLiteral("Empty"));

  expect(wait(h.svc.deleteTag(t2.id)));
  filterMap.clear();
  for (const TreeFilter& f : h.svc.treeFilters()) {
    filterMap[f.key] = f;
  }
  CHECK_FALSE(filterMap.contains(k2));
  CHECK(filterMap.contains(k1));
}

TEST_CASE("service tree_filters counts shared package ids across copies") {
  TestHarness h;
  h.ctx.add(QStringLiteral("u1"), QStringLiteral("Steam Copy"),
            QStringLiteral("shared.pkg"), QStringLiteral("steam"));
  h.ctx.add(QStringLiteral("u2"), QStringLiteral("Local Copy"),
            QStringLiteral("shared.pkg"), QStringLiteral("local"));
  h.ctx.add(QStringLiteral("u3"), QStringLiteral("Other Mod"),
            QStringLiteral("other.pkg"), QStringLiteral("local"));

  const Tag tag = expect(wait(
      h.svc.createTag(QStringLiteral("Overhaul"), QStringLiteral("#5eead4"))));
  expect(wait(h.svc.setModTags({QStringLiteral("shared.pkg")}, {tag.id}, {})));

  QHash<QString, int> counts;
  for (const TreeFilter& f : h.svc.treeFilters()) {
    counts[f.key] = f.count;
  }
  CHECK(counts.value(QStringLiteral("tag:%1").arg(tag.id)) == 2);
}

TEST_CASE("add_standard_rules is fresh and idempotent") {
  TestHarness h;
  const int total = static_cast<int>(standardRules().size());
  QSet<QString> names;
  for (const StandardRule& r : standardRules()) {
    names.insert(r.folder);
  }

  CHECK(expect(wait(h.svc.addStandardRules())) == total);
  CHECK(h.changes.count == 1);

  QSet<QString> topFolders;
  for (const Folder& f : h.svc.state().folders) {
    if (f.parentId == kRootId) {
      topFolders.insert(f.name);
    }
  }
  CHECK(topFolders == names);
  CHECK(h.svc.state().folders.size() == names.size() + 1);
  CHECK(h.svc.state().rules.size() == total);

  CHECK(expect(wait(h.svc.addStandardRules())) == 0);
  CHECK(h.changes.count == 1);
  CHECK(h.svc.state().rules.size() == total);
  CHECK(h.svc.state().folders.size() == names.size() + 1);
}

TEST_CASE(
    "add_standard_rules reuses existing folders and preserves user rules") {
  TestHarness h;
  const Folder official =
      expect(wait(h.svc.createFolder(QStringLiteral("OFFICIAL"))));
  const Folder mine = expect(wait(h.svc.createFolder(QStringLiteral("Mine"))));
  expect(wait(h.svc.setRules({
      RuleSpec{.field = RuleField::kAuthor,
               .op = RuleOp::kContains,
               .pattern = QStringLiteral("me"),
               .folderId = mine.id},
      RuleSpec{.field = RuleField::kPackageId,
               .op = RuleOp::kPrefix,
               .pattern = QStringLiteral("SARG."),
               .folderId = mine.id},
  })));
  h.changes.count = 0;

  const int total = static_cast<int>(standardRules().size());
  CHECK(expect(wait(h.svc.addStandardRules())) == total - 1);
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
  CHECK(h.svc.state().folders.value(rules[0].folderId).name ==
        QStringLiteral("Mine"));
  CHECK(rules[0].pattern == QStringLiteral("me"));
  CHECK(h.svc.state().folders.value(rules[1].folderId).name ==
        QStringLiteral("Mine"));
  CHECK(rules[1].pattern == QStringLiteral("SARG."));

  // The first standard rule for Official got re-targeted to the existing
  // OFFICIAL folder.
  CHECK(rules[2].folderId == official.id);
}

TEST_CASE("standard rules resolve mods correctly") {
  TestHarness h;
  expect(wait(h.svc.addStandardRules()));
  const OrganizerState& state = h.svc.state();

  const auto folderName = [&](const QString& pid,
                              const QString& name = QStringLiteral("Mod"),
                              const QString& author = {}) {
    const auto [fid, _] = folderFor(state, pid, name, author);
    return state.folders.value(fid).name;
  };

  CHECK(folderName(QStringLiteral("oskarpotocki.vfe.core")) ==
        QStringLiteral("Vanilla Expanded"));
  CHECK(folderName(QStringLiteral("someone.vfe"), QStringLiteral("Mod"),
                   QStringLiteral("Oskar Potocki, Taranchuk")) ==
        QStringLiteral("Vanilla Expanded"));
  CHECK(folderName(QStringLiteral("ludeon.rimworld.biotech"),
                   QStringLiteral("Biotech")) == QStringLiteral("Official"));
  CHECK(folderName(QStringLiteral("brrainz.harmony"),
                   QStringLiteral("Harmony")) ==
        QStringLiteral("Frameworks & Libraries"));
  CHECK(folderName(QStringLiteral("ceteam.combatextended"),
                   QStringLiteral("Combat Extended")) ==
        QStringLiteral("Combat Extended"));
  CHECK(folderName(QStringLiteral("sarg.alphagenes")) ==
        QStringLiteral("Alpha Mods"));
  CHECK(folderName(QStringLiteral("bs.performance"),
                   QStringLiteral("Performance Fish")) ==
        QStringLiteral("Performance"));
  CHECK(
      folderName(QStringLiteral("oskarpotocki.vanillafactionsexpanded.core")) ==
      QStringLiteral("Frameworks & Libraries"));
  CHECK(folderName(QStringLiteral("vanillaracesexpanded.sanguophage")) ==
        QStringLiteral("Vanilla Expanded"));
  CHECK(folderName(QStringLiteral("dubwise.dubsperformanceanalyzer.steam")) ==
        QStringLiteral("Performance"));
  CHECK(folderName(QStringLiteral("dubwise.rimatomics")) ==
        QStringLiteral("Dubs Mods"));
  CHECK(folderName(QStringLiteral("brrainz.achtung"),
                   QStringLiteral("Achtung!")) ==
        QStringLiteral("Quality of Life"));
  CHECK(folderName(QStringLiteral("brrainz.jobsofopportunity")) ==
        QStringLiteral("Root"));
  CHECK(folderName(QStringLiteral("smartkar.athena.framework"),
                   QStringLiteral("Athena Framework")) ==
        QStringLiteral("Root"));
  CHECK(folderName(QStringLiteral("someone.perf"),
                   QStringLiteral("Performance Tweaks")) ==
        QStringLiteral("Root"));
  CHECK(folderName(QStringLiteral("someone.other"), QStringLiteral("Other")) ==
        QStringLiteral("Root"));
}

TEST_CASE("standard package_id rules are not shadowed") {
  TestHarness h;
  expect(wait(h.svc.addStandardRules()));
  const OrganizerState& state = h.svc.state();

  for (const StandardRule& rule : standardRules()) {
    if (rule.field != RuleField::kPackageId) {
      continue;
    }
    const QString probe = rule.op == RuleOp::kEquals
                              ? rule.pattern
                              : rule.pattern + QStringLiteral("x");
    const auto [fid, _] = folderFor(state, probe, QStringLiteral("Mod"), {});
    CHECK(state.folders.value(fid).name == rule.folder);
  }
}
