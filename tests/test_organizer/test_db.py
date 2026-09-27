from __future__ import annotations

from pathlib import Path
from typing import cast

import pytest

from pxmodrim.core.organizer.db import OrganizerDb
from pxmodrim.core.organizer.models import (
    ROOT_ID,
    OrganizerError,
    RuleField,
    RuleSpec,
)


@pytest.fixture
def db(tmp_path: Path) -> OrganizerDb:
    return OrganizerDb(tmp_path / "organizer.db")


@pytest.mark.asyncio
async def test_fresh_db_and_root_invariants(db: OrganizerDb) -> None:
    try:
        state = await db.load()
        assert len(state.folders) == 1
        root = state.folders[ROOT_ID]
        assert root.id == ROOT_ID
        assert root.parent_id is None
        assert root.name == "Root"
        assert root.collapsed is False
        assert state.placements == {}
        assert state.tags == {}
        assert state.mod_tags == {}
        assert state.rules == ()

        with pytest.raises(OrganizerError):
            await db.delete_folder(ROOT_ID)

        with pytest.raises(OrganizerError):
            await db.rename_folder(ROOT_ID, "CustomRoot")

        with pytest.raises(OrganizerError):
            await db.set_parent(ROOT_ID, 2)
    finally:
        await db.close()


@pytest.mark.asyncio
async def test_duplicate_sibling_names_and_different_parents(db: OrganizerDb) -> None:
    try:
        f_a = await db.create_folder(ROOT_ID, "FolderA")
        assert f_a.id > ROOT_ID
        assert f_a.parent_id == ROOT_ID
        assert f_a.name == "FolderA"

        # Case-insensitive duplicate sibling under root
        with pytest.raises(OrganizerError):
            await db.create_folder(ROOT_ID, "foldera")

        # Create another sibling
        f_b = await db.create_folder(ROOT_ID, "FolderB")

        # Renaming sibling to duplicate existing sibling raises OrganizerError
        with pytest.raises(OrganizerError):
            await db.rename_folder(f_b.id, "foldera")

        # Same name under different parents is allowed
        f_sub = await db.create_folder(f_a.id, "FolderB")
        assert f_sub.parent_id == f_a.id
        assert f_sub.name == "FolderB"

        # Moving f_sub to root should fail because root already has FolderB
        with pytest.raises(OrganizerError):
            await db.set_parent(f_sub.id, ROOT_ID)
    finally:
        await db.close()


@pytest.mark.asyncio
async def test_placement_upsert_and_unplace(db: OrganizerDb) -> None:
    try:
        f1 = await db.create_folder(ROOT_ID, "Folder1")
        f2 = await db.create_folder(ROOT_ID, "Folder2")

        # Lowercases package IDs on place
        await db.place(["Package.One", "PACKAGE.TWO"], f1.id)
        st = await db.load()
        assert st.placements["package.one"] == f1.id
        assert st.placements["package.two"] == f1.id

        # Upsert: move Package.One to Folder2
        await db.place(["package.ONE"], f2.id)
        st = await db.load()
        assert st.placements["package.one"] == f2.id
        assert st.placements["package.two"] == f1.id

        # Unplace removes rows
        await db.unplace(["PACKAGE.ONE"])
        st = await db.load()
        assert "package.one" not in st.placements
        assert st.placements["package.two"] == f1.id

        # Placing into non-existent folder raises OrganizerError
        with pytest.raises(OrganizerError):
            await db.place(["package.three"], 9999)
    finally:
        await db.close()


@pytest.mark.asyncio
async def test_delete_folder_subtree_placements_and_descendants(
    db: OrganizerDb,
) -> None:
    try:
        # Build hierarchy: Root -> f_parent -> f_child -> f_grandchild
        f_parent = await db.create_folder(ROOT_ID, "Parent")
        f_child = await db.create_folder(f_parent.id, "Child")
        f_grandchild = await db.create_folder(f_child.id, "Grandchild")
        f_other = await db.create_folder(ROOT_ID, "Other")

        await db.place(["mod.parent"], f_parent.id)
        await db.place(["mod.child"], f_child.id)
        await db.place(["mod.grandchild"], f_grandchild.id)
        await db.place(["mod.other"], f_other.id)
        await db.place(["mod.root"], ROOT_ID)

        # Deleting parent moves subtree placements to ROOT_ID and drops descendants
        await db.delete_folder(f_parent.id)

        st = await db.load()
        assert f_parent.id not in st.folders
        assert f_child.id not in st.folders
        assert f_grandchild.id not in st.folders
        assert f_other.id in st.folders

        assert st.placements["mod.parent"] == ROOT_ID
        assert st.placements["mod.child"] == ROOT_ID
        assert st.placements["mod.grandchild"] == ROOT_ID
        assert st.placements["mod.other"] == f_other.id
        assert st.placements["mod.root"] == ROOT_ID

        # Deleting non-existent folder raises OrganizerError
        with pytest.raises(OrganizerError):
            await db.delete_folder(9999)
    finally:
        await db.close()


@pytest.mark.asyncio
async def test_tags_lifecycle_and_mod_tags(db: OrganizerDb) -> None:
    try:
        t1 = await db.create_tag("Tag1", "#ff0000")
        t2 = await db.create_tag("Tag2", "#00ff00")
        assert t1.name == "Tag1"
        assert t1.color == "#ff0000"

        # Duplicate tag name raises OrganizerError
        with pytest.raises(OrganizerError):
            await db.create_tag("tag1", "#123456")

        # Set mod tags
        await db.set_mod_tags(["Mod.Alpha", "Mod.Beta"], add=[t1.id, t2.id])
        st = await db.load()
        assert st.mod_tags["mod.alpha"] == frozenset({t1.id, t2.id})
        assert st.mod_tags["mod.beta"] == frozenset({t1.id, t2.id})

        # Remove a tag from Mod.Alpha
        await db.set_mod_tags(["mod.alpha"], remove=[t1.id])
        st = await db.load()
        assert st.mod_tags["mod.alpha"] == frozenset({t2.id})

        # Invalid tag ID in add raises OrganizerError
        with pytest.raises(OrganizerError):
            await db.set_mod_tags(["mod.alpha"], add=[9999])

        # Update tag
        await db.update_tag(t1.id, "Tag1Updated", "#aaaaaa")
        st = await db.load()
        assert st.tags[t1.id].name == "Tag1Updated"

        # Update tag to duplicate raises OrganizerError
        with pytest.raises(OrganizerError):
            await db.update_tag(t1.id, "tag2", "#aaaaaa")

        # Deleting tag removes it and cascades removal in mod_tags
        await db.delete_tag(t2.id)
        st = await db.load()
        assert t2.id not in st.tags
        assert "mod.alpha" not in st.mod_tags or st.mod_tags["mod.alpha"] == frozenset()
        assert st.mod_tags["mod.beta"] == frozenset({t1.id})

        # Deleting non-existent tag raises OrganizerError
        with pytest.raises(OrganizerError):
            await db.delete_tag(9999)
    finally:
        await db.close()


@pytest.mark.asyncio
async def test_replace_rules_and_cascade_on_delete_folder(db: OrganizerDb) -> None:
    try:
        f1 = await db.create_folder(ROOT_ID, "Target1")
        f2 = await db.create_folder(ROOT_ID, "Target2")

        spec1 = RuleSpec(field="name", op="prefix", pattern="Vanilla", folder_id=f1.id)
        spec2 = RuleSpec(
            field="author", op="contains", pattern="Oskar", folder_id=f2.id
        )
        spec3 = RuleSpec(
            field="package_id",
            op="equals",
            pattern="ludeon.rimworld",
            folder_id=f1.id,
        )

        rules = await db.replace_rules([spec1, spec2, spec3])
        assert len(rules) == 3
        assert [r.position for r in rules] == [0, 1, 2]
        assert rules[0].field == "name"
        assert rules[1].field == "author"
        assert rules[2].field == "package_id"

        st = await db.load()
        assert st.rules == rules

        # Invalid field or op raises OrganizerError
        with pytest.raises(OrganizerError):
            bad_spec = RuleSpec(
                field=cast(RuleField, "invalid"),
                op="prefix",
                pattern="x",
                folder_id=f1.id,
            )
            await db.replace_rules([bad_spec])
        with pytest.raises(OrganizerError):
            bad_spec = RuleSpec(field="name", op="prefix", pattern="x", folder_id=9999)
            await db.replace_rules([bad_spec])

        # Deleting Target1 drops rule 0 and 2; rule 1 targeting Target2 remains
        await db.delete_folder(f1.id)
        st = await db.load()
        assert len(st.rules) == 1
        assert st.rules[0].id == rules[1].id
        assert st.rules[0].folder_id == f2.id
    finally:
        await db.close()


@pytest.mark.asyncio
async def test_state_survives_close_and_reopen(tmp_path: Path) -> None:
    db_file = tmp_path / "organizer.db"
    db1 = OrganizerDb(db_file)
    try:
        f = await db1.create_folder(ROOT_ID, "PersistFolder")
        await db1.set_collapsed(f.id, True)
        await db1.place(["persist.pkg"], f.id)
        tag = await db1.create_tag("PersistTag", "#abcdef")
        await db1.set_mod_tags(["persist.pkg"], add=[tag.id])
        await db1.replace_rules(
            [RuleSpec(field="name", op="contains", pattern="mod", folder_id=f.id)]
        )
    finally:
        await db1.close()

    db2 = OrganizerDb(db_file)
    try:
        st = await db2.load()
        assert f.id in st.folders
        assert st.folders[f.id].name == "PersistFolder"
        assert st.folders[f.id].collapsed is True
        assert st.placements["persist.pkg"] == f.id
        assert tag.id in st.tags
        assert st.tags[tag.id].name == "PersistTag"
        assert st.mod_tags["persist.pkg"] == frozenset({tag.id})
        assert len(st.rules) == 1
        assert st.rules[0].pattern == "mod"
        assert st.rules[0].folder_id == f.id
    finally:
        db2.close_sync()
