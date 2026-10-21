#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include "core/organizer/db.hpp"
#include "core/organizer/defaults.hpp"
#include "core/organizer/models.hpp"
#include "test_util.hpp"

using namespace px::modrim::organizer;
using organizer_test::expect;
using organizer_test::expectError;
using organizer_test::wait;
using test_support::TestApplication;

namespace {

QString organizerFile(const QTemporaryDir& dir) {
  return dir.filePath(QStringLiteral("organizer.db"));
}

}  // namespace

TEST_CASE("a fresh database holds only the root folder") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  OrganizerDb db(organizerFile(dir));

  const OrganizerState state = expect(wait(db.load()));
  REQUIRE(state.folders.size() == 1);
  const Folder root = state.folders.value(kRootId);
  CHECK(root.id == kRootId);
  CHECK_FALSE(root.parentId.has_value());
  CHECK(root.name == QStringLiteral("Root"));
  CHECK_FALSE(root.collapsed);
  CHECK(state.placements.isEmpty());
  CHECK(state.tags.isEmpty());
  CHECK(state.modTags.isEmpty());
  CHECK(state.rules.isEmpty());

  expectError(wait(db.deleteFolder(kRootId)));
  expectError(wait(db.renameFolder(kRootId, QStringLiteral("CustomRoot"))));
  expectError(wait(db.setParent(kRootId, 2)));
}

TEST_CASE("sibling names are unique case-insensitively, per parent") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  OrganizerDb db(organizerFile(dir));

  const Folder fA =
      expect(wait(db.createFolder(kRootId, QStringLiteral("FolderA"))));
  CHECK(fA.id > kRootId);
  CHECK(fA.parentId == kRootId);
  CHECK(fA.name == QStringLiteral("FolderA"));

  expectError(wait(db.createFolder(kRootId, QStringLiteral("foldera"))));

  const Folder fB =
      expect(wait(db.createFolder(kRootId, QStringLiteral("FolderB"))));
  expectError(wait(db.renameFolder(fB.id, QStringLiteral("foldera"))));

  // The same name is fine under a different parent.
  const Folder fSub =
      expect(wait(db.createFolder(fA.id, QStringLiteral("FolderB"))));
  CHECK(fSub.parentId == fA.id);
  CHECK(fSub.name == QStringLiteral("FolderB"));

  expectError(wait(db.setParent(fSub.id, kRootId)));
}

TEST_CASE(
    "placing lowercases package ids and upserts; unplacing removes rows") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  OrganizerDb db(organizerFile(dir));

  const Folder f1 =
      expect(wait(db.createFolder(kRootId, QStringLiteral("Folder1"))));
  const Folder f2 =
      expect(wait(db.createFolder(kRootId, QStringLiteral("Folder2"))));

  expect(wait(db.place({}, 9999)));
  expect(wait(db.place(
      {QStringLiteral("Package.One"), QStringLiteral("PACKAGE.TWO")}, f1.id)));
  OrganizerState st = expect(wait(db.load()));
  CHECK(st.placements.value(QStringLiteral("package.one")) == f1.id);
  CHECK(st.placements.value(QStringLiteral("package.two")) == f1.id);

  // Upsert: moving an existing placement.
  expect(wait(db.place({QStringLiteral("package.ONE")}, f2.id)));
  st = expect(wait(db.load()));
  CHECK(st.placements.value(QStringLiteral("package.one")) == f2.id);
  CHECK(st.placements.value(QStringLiteral("package.two")) == f1.id);

  expect(wait(db.unplace({QStringLiteral("PACKAGE.ONE")})));
  st = expect(wait(db.load()));
  CHECK_FALSE(st.placements.contains(QStringLiteral("package.one")));
  CHECK(st.placements.value(QStringLiteral("package.two")) == f1.id);

  expectError(wait(db.place({QStringLiteral("package.three")}, 9999)));
}

TEST_CASE(
    "create_folder_with places its mods and rolls back on a duplicate name") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  OrganizerDb db(organizerFile(dir));

  const Folder f = expect(wait(db.createFolderWith(
      kRootId, QStringLiteral("CoreMods"),
      {QStringLiteral("Package.One"), QStringLiteral("package.two"),
       QStringLiteral("PACKAGE.THREE")})));
  CHECK(f.id > kRootId);
  CHECK(f.name == QStringLiteral("CoreMods"));
  CHECK(f.parentId == kRootId);

  const OrganizerState st = expect(wait(db.load()));
  CHECK(st.folders.contains(f.id));
  CHECK(st.placements.value(QStringLiteral("package.one")) == f.id);
  CHECK(st.placements.value(QStringLiteral("package.two")) == f.id);
  CHECK(st.placements.value(QStringLiteral("package.three")) == f.id);

  expectError(wait(db.createFolderWith(
      kRootId, QStringLiteral("coremods"),
      {QStringLiteral("package.four"), QStringLiteral("package.five")})));

  const OrganizerState after = expect(wait(db.load()));
  CHECK_FALSE(after.placements.contains(QStringLiteral("package.four")));
  CHECK_FALSE(after.placements.contains(QStringLiteral("package.five")));
  CHECK(after.folders.size() == st.folders.size());
}

TEST_CASE(
    "deleting a folder re-homes its subtree's placements and drops its "
    "descendants") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  OrganizerDb db(organizerFile(dir));

  const Folder fParent =
      expect(wait(db.createFolder(kRootId, QStringLiteral("Parent"))));
  const Folder fChild =
      expect(wait(db.createFolder(fParent.id, QStringLiteral("Child"))));
  const Folder fGrandchild =
      expect(wait(db.createFolder(fChild.id, QStringLiteral("Grandchild"))));
  const Folder fOther =
      expect(wait(db.createFolder(kRootId, QStringLiteral("Other"))));

  expect(wait(db.place({QStringLiteral("mod.parent")}, fParent.id)));
  expect(wait(db.place({QStringLiteral("mod.child")}, fChild.id)));
  expect(wait(db.place({QStringLiteral("mod.grandchild")}, fGrandchild.id)));
  expect(wait(db.place({QStringLiteral("mod.other")}, fOther.id)));
  expect(wait(db.place({QStringLiteral("mod.root")}, kRootId)));

  expect(wait(db.deleteFolder(fParent.id)));

  const OrganizerState st = expect(wait(db.load()));
  CHECK_FALSE(st.folders.contains(fParent.id));
  CHECK_FALSE(st.folders.contains(fChild.id));
  CHECK_FALSE(st.folders.contains(fGrandchild.id));
  CHECK(st.folders.contains(fOther.id));

  CHECK(st.placements.value(QStringLiteral("mod.parent")) == kRootId);
  CHECK(st.placements.value(QStringLiteral("mod.child")) == kRootId);
  CHECK(st.placements.value(QStringLiteral("mod.grandchild")) == kRootId);
  CHECK(st.placements.value(QStringLiteral("mod.other")) == fOther.id);
  CHECK(st.placements.value(QStringLiteral("mod.root")) == kRootId);

  expectError(wait(db.deleteFolder(9999)));
}

TEST_CASE(
    "tags: create, rename, unique names, mod tagging and cascade on delete") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  OrganizerDb db(organizerFile(dir));

  const Tag t1 = expect(
      wait(db.createTag(QStringLiteral("Tag1"), QStringLiteral("#ff0000"))));
  const Tag t2 = expect(
      wait(db.createTag(QStringLiteral("Tag2"), QStringLiteral("#00ff00"))));
  CHECK(t1.name == QStringLiteral("Tag1"));
  CHECK(t1.color == QStringLiteral("#ff0000"));

  expectError(
      wait(db.createTag(QStringLiteral("tag1"), QStringLiteral("#123456"))));

  expect(wait(
      db.setModTags({QStringLiteral("Mod.Alpha"), QStringLiteral("Mod.Beta")},
                    {t1.id, t2.id}, {})));
  OrganizerState st = expect(wait(db.load()));
  CHECK(st.modTags.value(QStringLiteral("mod.alpha")) ==
        QSet<qint64>{t1.id, t2.id});
  CHECK(st.modTags.value(QStringLiteral("mod.beta")) ==
        QSet<qint64>{t1.id, t2.id});

  expect(wait(db.setModTags({QStringLiteral("mod.alpha")}, {}, {t1.id})));
  st = expect(wait(db.load()));
  CHECK(st.modTags.value(QStringLiteral("mod.alpha")) == QSet<qint64>{t2.id});

  expectError(wait(db.setModTags({QStringLiteral("mod.alpha")}, {9999}, {})));

  expect(wait(db.updateTag(t1.id, QStringLiteral("Tag1Updated"),
                           QStringLiteral("#aaaaaa"))));
  st = expect(wait(db.load()));
  CHECK(st.tags.value(t1.id).name == QStringLiteral("Tag1Updated"));

  expectError(wait(
      db.updateTag(t1.id, QStringLiteral("tag2"), QStringLiteral("#aaaaaa"))));

  expect(wait(db.deleteTag(t2.id)));
  st = expect(wait(db.load()));
  CHECK_FALSE(st.tags.contains(t2.id));
  CHECK((!st.modTags.contains(QStringLiteral("mod.alpha")) ||
         st.modTags.value(QStringLiteral("mod.alpha")).isEmpty()));
  CHECK(st.modTags.value(QStringLiteral("mod.beta")) == QSet<qint64>{t1.id});

  expectError(wait(db.deleteTag(9999)));
}

TEST_CASE(
    "replacing rules numbers them by position and deleting a folder drops its "
    "rules") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  OrganizerDb db(organizerFile(dir));

  const Folder f1 =
      expect(wait(db.createFolder(kRootId, QStringLiteral("Target1"))));
  const Folder f2 =
      expect(wait(db.createFolder(kRootId, QStringLiteral("Target2"))));

  const QList<RuleSpec> specs{
      RuleSpec{.field = RuleField::kName,
               .op = RuleOp::kPrefix,
               .pattern = QStringLiteral("Vanilla"),
               .folderId = f1.id},
      RuleSpec{.field = RuleField::kAuthor,
               .op = RuleOp::kContains,
               .pattern = QStringLiteral("Oskar"),
               .folderId = f2.id},
      RuleSpec{.field = RuleField::kPackageId,
               .op = RuleOp::kEquals,
               .pattern = QStringLiteral("ludeon.rimworld"),
               .folderId = f1.id},
  };
  expect(wait(db.replaceRules(specs)));

  const OrganizerState st = expect(wait(db.load()));
  CHECK(st.rules == specs);

  // A rule pointing at a missing folder fails the whole replacement.
  expectError(wait(db.replaceRules({RuleSpec{.field = RuleField::kName,
                                             .op = RuleOp::kPrefix,
                                             .pattern = QStringLiteral("x"),
                                             .folderId = 9999}})));

  // Deleting Target1 drops its rules; the rule for Target2 remains.
  expect(wait(db.deleteFolder(f1.id)));
  const OrganizerState after = expect(wait(db.load()));
  REQUIRE(after.rules.size() == 1);
  CHECK(after.rules[0] == specs[1]);
  CHECK(after.rules[0].folderId == f2.id);
}

TEST_CASE("state survives closing and reopening the database") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString file = organizerFile(dir);

  Folder folder;
  Tag tag;
  {
    OrganizerDb db1(file);
    folder = expect(
        wait(db1.createFolder(kRootId, QStringLiteral("PersistFolder"))));
    expect(wait(db1.setCollapsed(folder.id, true)));
    expect(wait(db1.place({QStringLiteral("persist.pkg")}, folder.id)));
    tag = expect(wait(db1.createTag(QStringLiteral("PersistTag"),
                                    QStringLiteral("#abcdef"))));
    expect(wait(db1.setModTags({QStringLiteral("persist.pkg")}, {tag.id}, {})));
    expect(wait(db1.replaceRules({RuleSpec{.field = RuleField::kName,
                                           .op = RuleOp::kContains,
                                           .pattern = QStringLiteral("mod"),
                                           .folderId = folder.id}})));
    wait(db1.close());
  }

  OrganizerDb db2(file);
  const OrganizerState st = expect(wait(db2.load()));
  REQUIRE(st.folders.contains(folder.id));
  CHECK(st.folders.value(folder.id).name == QStringLiteral("PersistFolder"));
  CHECK(st.folders.value(folder.id).collapsed);
  CHECK(st.placements.value(QStringLiteral("persist.pkg")) == folder.id);
  REQUIRE(st.tags.contains(tag.id));
  CHECK(st.tags.value(tag.id).name == QStringLiteral("PersistTag"));
  CHECK(st.modTags.value(QStringLiteral("persist.pkg")) ==
        QSet<qint64>{tag.id});
  REQUIRE(st.rules.size() == 1);
  CHECK(st.rules[0].pattern == QStringLiteral("mod"));
  CHECK(st.rules[0].folderId == folder.id);
}

TEST_CASE(
    "append_named_rules is atomic and resolves folders case-insensitively") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  OrganizerDb db(organizerFile(dir));

  const Folder existing =
      expect(wait(db.createFolder(kRootId, QStringLiteral("Existing"))));
  expect(wait(db.replaceRules({RuleSpec{.field = RuleField::kName,
                                        .op = RuleOp::kContains,
                                        .pattern = QStringLiteral("a"),
                                        .folderId = existing.id}})));
  const OrganizerState before = expect(wait(db.load()));

  expectError(wait(db.appendNamedRules(
                  kRootId, {QStringLiteral("New")},
                  {StandardRule{.folder = QStringLiteral("New"),
                                .field = RuleField::kName,
                                .op = RuleOp::kContains,
                                .pattern = QStringLiteral("b")},
                   StandardRule{.folder = QStringLiteral("Missing"),
                                .field = RuleField::kName,
                                .op = RuleOp::kContains,
                                .pattern = QStringLiteral("c")}})),
              QStringLiteral("Missing"));
  CHECK(expect(wait(db.load())) == before);

  expect(wait(
      db.appendNamedRules(kRootId, {QStringLiteral("New")},
                          {StandardRule{.folder = QStringLiteral("existing"),
                                        .field = RuleField::kName,
                                        .op = RuleOp::kContains,
                                        .pattern = QStringLiteral("b")},
                           StandardRule{.folder = QStringLiteral("NEW"),
                                        .field = RuleField::kName,
                                        .op = RuleOp::kContains,
                                        .pattern = QStringLiteral("c")}})));

  const OrganizerState st = expect(wait(db.load()));
  qint64 newId = -1;
  for (const Folder& folder : st.folders) {
    if (folder.name == QStringLiteral("New")) {
      newId = folder.id;
    }
  }
  REQUIRE(st.rules.size() == 3);
  CHECK(st.rules[0].pattern == QStringLiteral("a"));
  CHECK(st.rules[0].folderId == existing.id);
  CHECK(st.rules[1].pattern == QStringLiteral("b"));
  CHECK(st.rules[1].folderId == existing.id);
  CHECK(st.rules[2].pattern == QStringLiteral("c"));
  CHECK(st.rules[2].folderId == newId);
}

TEST_CASE("the database file is created in a missing directory on first use") {
  TestApplication app;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString file =
      dir.filePath(QStringLiteral("nested/config/organizer.db"));
  OrganizerDb db(file);

  expect(wait(db.createFolder(kRootId, QStringLiteral("Made"))));
  CHECK(QFile::exists(file));
}
