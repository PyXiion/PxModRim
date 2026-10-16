#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "core/organizer/db.h"
#include "core/organizer/defaults.h"
#include "core/organizer/models.h"
#include "test_util.h"

using namespace px::modrim::organizer;
using organizer_test::expect;
using organizer_test::expect_error;
using organizer_test::wait;
using test_support::TestApplication;

namespace {

QString organizer_file(const QTemporaryDir& dir) {
    return dir.filePath(QStringLiteral("organizer.db"));
}

}  // namespace

TEST_CASE("a fresh database holds only the root folder") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    OrganizerDb db(organizer_file(dir));

    const OrganizerState state = expect(wait(db.load()));
    REQUIRE(state.folders.size() == 1);
    const Folder root = state.folders.value(ROOT_ID);
    CHECK(root.id == ROOT_ID);
    CHECK_FALSE(root.parent_id.has_value());
    CHECK(root.name == QStringLiteral("Root"));
    CHECK_FALSE(root.collapsed);
    CHECK(state.placements.isEmpty());
    CHECK(state.tags.isEmpty());
    CHECK(state.mod_tags.isEmpty());
    CHECK(state.rules.isEmpty());

    expect_error(wait(db.delete_folder(ROOT_ID)));
    expect_error(wait(db.rename_folder(ROOT_ID, QStringLiteral("CustomRoot"))));
    expect_error(wait(db.set_parent(ROOT_ID, 2)));
}

TEST_CASE("sibling names are unique case-insensitively, per parent") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    OrganizerDb db(organizer_file(dir));

    const Folder f_a = expect(wait(db.create_folder(ROOT_ID, QStringLiteral("FolderA"))));
    CHECK(f_a.id > ROOT_ID);
    CHECK(f_a.parent_id == ROOT_ID);
    CHECK(f_a.name == QStringLiteral("FolderA"));

    expect_error(wait(db.create_folder(ROOT_ID, QStringLiteral("foldera"))));

    const Folder f_b = expect(wait(db.create_folder(ROOT_ID, QStringLiteral("FolderB"))));
    expect_error(wait(db.rename_folder(f_b.id, QStringLiteral("foldera"))));

    // The same name is fine under a different parent.
    const Folder f_sub = expect(wait(db.create_folder(f_a.id, QStringLiteral("FolderB"))));
    CHECK(f_sub.parent_id == f_a.id);
    CHECK(f_sub.name == QStringLiteral("FolderB"));

    expect_error(wait(db.set_parent(f_sub.id, ROOT_ID)));
}

TEST_CASE("placing lowercases package ids and upserts; unplacing removes rows") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    OrganizerDb db(organizer_file(dir));

    const Folder f1 = expect(wait(db.create_folder(ROOT_ID, QStringLiteral("Folder1"))));
    const Folder f2 = expect(wait(db.create_folder(ROOT_ID, QStringLiteral("Folder2"))));

    expect(wait(db.place({}, 9999)));
    expect(wait(db.place({QStringLiteral("Package.One"), QStringLiteral("PACKAGE.TWO")}, f1.id)));
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

    expect_error(wait(db.place({QStringLiteral("package.three")}, 9999)));
}

TEST_CASE("create_folder_with places its mods and rolls back on a duplicate name") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    OrganizerDb db(organizer_file(dir));

    const Folder f = expect(wait(db.create_folder_with(
        ROOT_ID, QStringLiteral("CoreMods"),
        {QStringLiteral("Package.One"), QStringLiteral("package.two"), QStringLiteral("PACKAGE.THREE")})));
    CHECK(f.id > ROOT_ID);
    CHECK(f.name == QStringLiteral("CoreMods"));
    CHECK(f.parent_id == ROOT_ID);

    const OrganizerState st = expect(wait(db.load()));
    CHECK(st.folders.contains(f.id));
    CHECK(st.placements.value(QStringLiteral("package.one")) == f.id);
    CHECK(st.placements.value(QStringLiteral("package.two")) == f.id);
    CHECK(st.placements.value(QStringLiteral("package.three")) == f.id);

    expect_error(wait(db.create_folder_with(ROOT_ID, QStringLiteral("coremods"),
                                            {QStringLiteral("package.four"), QStringLiteral("package.five")})));

    const OrganizerState after = expect(wait(db.load()));
    CHECK_FALSE(after.placements.contains(QStringLiteral("package.four")));
    CHECK_FALSE(after.placements.contains(QStringLiteral("package.five")));
    CHECK(after.folders.size() == st.folders.size());
}

TEST_CASE("deleting a folder re-homes its subtree's placements and drops its descendants") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    OrganizerDb db(organizer_file(dir));

    const Folder f_parent = expect(wait(db.create_folder(ROOT_ID, QStringLiteral("Parent"))));
    const Folder f_child = expect(wait(db.create_folder(f_parent.id, QStringLiteral("Child"))));
    const Folder f_grandchild = expect(wait(db.create_folder(f_child.id, QStringLiteral("Grandchild"))));
    const Folder f_other = expect(wait(db.create_folder(ROOT_ID, QStringLiteral("Other"))));

    expect(wait(db.place({QStringLiteral("mod.parent")}, f_parent.id)));
    expect(wait(db.place({QStringLiteral("mod.child")}, f_child.id)));
    expect(wait(db.place({QStringLiteral("mod.grandchild")}, f_grandchild.id)));
    expect(wait(db.place({QStringLiteral("mod.other")}, f_other.id)));
    expect(wait(db.place({QStringLiteral("mod.root")}, ROOT_ID)));

    expect(wait(db.delete_folder(f_parent.id)));

    const OrganizerState st = expect(wait(db.load()));
    CHECK_FALSE(st.folders.contains(f_parent.id));
    CHECK_FALSE(st.folders.contains(f_child.id));
    CHECK_FALSE(st.folders.contains(f_grandchild.id));
    CHECK(st.folders.contains(f_other.id));

    CHECK(st.placements.value(QStringLiteral("mod.parent")) == ROOT_ID);
    CHECK(st.placements.value(QStringLiteral("mod.child")) == ROOT_ID);
    CHECK(st.placements.value(QStringLiteral("mod.grandchild")) == ROOT_ID);
    CHECK(st.placements.value(QStringLiteral("mod.other")) == f_other.id);
    CHECK(st.placements.value(QStringLiteral("mod.root")) == ROOT_ID);

    expect_error(wait(db.delete_folder(9999)));
}

TEST_CASE("tags: create, rename, unique names, mod tagging and cascade on delete") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    OrganizerDb db(organizer_file(dir));

    const Tag t1 = expect(wait(db.create_tag(QStringLiteral("Tag1"), QStringLiteral("#ff0000"))));
    const Tag t2 = expect(wait(db.create_tag(QStringLiteral("Tag2"), QStringLiteral("#00ff00"))));
    CHECK(t1.name == QStringLiteral("Tag1"));
    CHECK(t1.color == QStringLiteral("#ff0000"));

    expect_error(wait(db.create_tag(QStringLiteral("tag1"), QStringLiteral("#123456"))));

    expect(wait(db.set_mod_tags({QStringLiteral("Mod.Alpha"), QStringLiteral("Mod.Beta")}, {t1.id, t2.id}, {})));
    OrganizerState st = expect(wait(db.load()));
    CHECK(st.mod_tags.value(QStringLiteral("mod.alpha")) == QSet<qint64>{t1.id, t2.id});
    CHECK(st.mod_tags.value(QStringLiteral("mod.beta")) == QSet<qint64>{t1.id, t2.id});

    expect(wait(db.set_mod_tags({QStringLiteral("mod.alpha")}, {}, {t1.id})));
    st = expect(wait(db.load()));
    CHECK(st.mod_tags.value(QStringLiteral("mod.alpha")) == QSet<qint64>{t2.id});

    expect_error(wait(db.set_mod_tags({QStringLiteral("mod.alpha")}, {9999}, {})));

    expect(wait(db.update_tag(t1.id, QStringLiteral("Tag1Updated"), QStringLiteral("#aaaaaa"))));
    st = expect(wait(db.load()));
    CHECK(st.tags.value(t1.id).name == QStringLiteral("Tag1Updated"));

    expect_error(wait(db.update_tag(t1.id, QStringLiteral("tag2"), QStringLiteral("#aaaaaa"))));

    expect(wait(db.delete_tag(t2.id)));
    st = expect(wait(db.load()));
    CHECK_FALSE(st.tags.contains(t2.id));
    CHECK((!st.mod_tags.contains(QStringLiteral("mod.alpha")) ||
           st.mod_tags.value(QStringLiteral("mod.alpha")).isEmpty()));
    CHECK(st.mod_tags.value(QStringLiteral("mod.beta")) == QSet<qint64>{t1.id});

    expect_error(wait(db.delete_tag(9999)));
}

TEST_CASE("replacing rules numbers them by position and deleting a folder drops its rules") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    OrganizerDb db(organizer_file(dir));

    const Folder f1 = expect(wait(db.create_folder(ROOT_ID, QStringLiteral("Target1"))));
    const Folder f2 = expect(wait(db.create_folder(ROOT_ID, QStringLiteral("Target2"))));

    const QList<RuleSpec> specs{
        RuleSpec{.field = RuleField::Name, .op = RuleOp::Prefix, .pattern = QStringLiteral("Vanilla"), .folder_id = f1.id},
        RuleSpec{.field = RuleField::Author, .op = RuleOp::Contains, .pattern = QStringLiteral("Oskar"), .folder_id = f2.id},
        RuleSpec{.field = RuleField::PackageId, .op = RuleOp::Equals, .pattern = QStringLiteral("ludeon.rimworld"), .folder_id = f1.id},
    };
    const QList<Rule> rules = expect(wait(db.replace_rules(specs)));
    REQUIRE(rules.size() == 3);
    CHECK(rules[0].position == 0);
    CHECK(rules[1].position == 1);
    CHECK(rules[2].position == 2);
    CHECK(rules[0].field == RuleField::Name);
    CHECK(rules[1].field == RuleField::Author);
    CHECK(rules[2].field == RuleField::PackageId);

    const OrganizerState st = expect(wait(db.load()));
    CHECK(st.rules == rules);

    // A rule pointing at a missing folder fails the whole replacement.
    expect_error(wait(db.replace_rules({RuleSpec{.field = RuleField::Name, .op = RuleOp::Prefix,
                                                 .pattern = QStringLiteral("x"), .folder_id = 9999}})));

    // Deleting Target1 drops its rules; the rule for Target2 remains.
    expect(wait(db.delete_folder(f1.id)));
    const OrganizerState after = expect(wait(db.load()));
    REQUIRE(after.rules.size() == 1);
    CHECK(after.rules[0].id == rules[1].id);
    CHECK(after.rules[0].folder_id == f2.id);
}

TEST_CASE("state survives closing and reopening the database") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString file = organizer_file(dir);

    Folder folder;
    Tag tag;
    {
        OrganizerDb db1(file);
        folder = expect(wait(db1.create_folder(ROOT_ID, QStringLiteral("PersistFolder"))));
        expect(wait(db1.set_collapsed(folder.id, true)));
        expect(wait(db1.place({QStringLiteral("persist.pkg")}, folder.id)));
        tag = expect(wait(db1.create_tag(QStringLiteral("PersistTag"), QStringLiteral("#abcdef"))));
        expect(wait(db1.set_mod_tags({QStringLiteral("persist.pkg")}, {tag.id}, {})));
        expect(wait(db1.replace_rules({RuleSpec{.field = RuleField::Name, .op = RuleOp::Contains,
                                                .pattern = QStringLiteral("mod"), .folder_id = folder.id}})));
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
    CHECK(st.mod_tags.value(QStringLiteral("persist.pkg")) == QSet<qint64>{tag.id});
    REQUIRE(st.rules.size() == 1);
    CHECK(st.rules[0].pattern == QStringLiteral("mod"));
    CHECK(st.rules[0].folder_id == folder.id);
}

TEST_CASE("append_named_rules is atomic and resolves folders case-insensitively") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    OrganizerDb db(organizer_file(dir));

    const Folder existing = expect(wait(db.create_folder(ROOT_ID, QStringLiteral("Existing"))));
    expect(wait(db.replace_rules({RuleSpec{.field = RuleField::Name, .op = RuleOp::Contains,
                                           .pattern = QStringLiteral("a"), .folder_id = existing.id}})));
    const OrganizerState before = expect(wait(db.load()));

    expect_error(wait(db.append_named_rules(
                     ROOT_ID, {QStringLiteral("New")},
                     {StandardRule{.folder = QStringLiteral("New"), .field = RuleField::Name,
                                   .op = RuleOp::Contains, .pattern = QStringLiteral("b")},
                      StandardRule{.folder = QStringLiteral("Missing"), .field = RuleField::Name,
                                   .op = RuleOp::Contains, .pattern = QStringLiteral("c")}})),
                 QStringLiteral("Missing"));
    CHECK(expect(wait(db.load())) == before);

    expect(wait(db.append_named_rules(
        ROOT_ID, {QStringLiteral("New")},
        {StandardRule{.folder = QStringLiteral("existing"), .field = RuleField::Name,
                      .op = RuleOp::Contains, .pattern = QStringLiteral("b")},
         StandardRule{.folder = QStringLiteral("NEW"), .field = RuleField::Name,
                      .op = RuleOp::Contains, .pattern = QStringLiteral("c")}})));

    const OrganizerState st = expect(wait(db.load()));
    qint64 new_id = -1;
    for (const Folder& folder : st.folders) {
        if (folder.name == QStringLiteral("New")) {
            new_id = folder.id;
        }
    }
    REQUIRE(st.rules.size() == 3);
    CHECK(st.rules[0].position == 0);
    CHECK(st.rules[0].pattern == QStringLiteral("a"));
    CHECK(st.rules[0].folder_id == existing.id);
    CHECK(st.rules[1].position == 1);
    CHECK(st.rules[1].pattern == QStringLiteral("b"));
    CHECK(st.rules[1].folder_id == existing.id);
    CHECK(st.rules[2].position == 2);
    CHECK(st.rules[2].pattern == QStringLiteral("c"));
    CHECK(st.rules[2].folder_id == new_id);
}

TEST_CASE("the database file is created in a missing directory on first use") {
    TestApplication app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("nested/config/organizer.db"));
    OrganizerDb db(file);

    expect(wait(db.create_folder(ROOT_ID, QStringLiteral("Made"))));
    CHECK(QFile::exists(file));
}
