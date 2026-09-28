from __future__ import annotations

from collections.abc import AsyncIterator
from pathlib import Path
from typing import TYPE_CHECKING, cast

import pytest

from pxmodrim.core.models.metadata.structures import (
    AboutXmlMod,
    CaseInsensitiveStr,
    ListedMod,
)
from pxmodrim.core.organizer.db import OrganizerDb
from pxmodrim.core.organizer.defaults import STANDARD_RULES
from pxmodrim.core.organizer.models import ROOT_ID, OrganizerError, RuleSpec
from pxmodrim.core.organizer.resolve import folder_for
from pxmodrim.core.organizer.service import OrganizerService

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext


class _FakeCtx:
    def __init__(self) -> None:
        self.all_mods: dict[str, ListedMod] = {}
        self.active_uuids: list[str] = []

    def add(self, uuid: str, name: str, pid: str) -> None:
        self.all_mods[uuid] = AboutXmlMod(
            name=name, _uuid=uuid, package_id=CaseInsensitiveStr(pid)
        )


class _Counter:
    def __init__(self) -> None:
        self.count = 0

    def __call__(self, _: None) -> None:
        self.count += 1


@pytest.fixture
def ctx() -> _FakeCtx:
    return _FakeCtx()


@pytest.fixture
async def svc(tmp_path: Path, ctx: _FakeCtx) -> AsyncIterator[OrganizerService]:
    service = OrganizerService(
        cast("CoreContext", ctx), OrganizerDb(tmp_path / "organizer.db")
    )
    await service.init(cast("CoreContext", ctx))
    yield service
    await service.shutdown()


@pytest.fixture
def changes(svc: OrganizerService) -> _Counter:
    counter = _Counter()
    svc.changed.connect(counter)
    return counter


async def test_bulk_folder_expansion_persists(svc: OrganizerService) -> None:
    parent = await svc.create_folder("Parent")
    child = await svc.create_folder("Child", parent.id)
    other = await svc.create_folder("Other")

    await svc.set_all_collapsed(True)
    persisted = await svc._db.load()
    assert all(
        persisted.folders[folder.id].collapsed for folder in (parent, child, other)
    )

    await svc.set_collapsed(child.id, False)
    await svc.set_all_collapsed(False)
    persisted = await svc._db.load()
    assert all(
        not persisted.folders[folder.id].collapsed for folder in (parent, child, other)
    )


async def test_depth_limit(svc: OrganizerService, changes: _Counter) -> None:
    a = await svc.create_folder("A")
    b = await svc.create_folder("B", a.id)
    c = await svc.create_folder("C", b.id)
    assert changes.count == 3
    with pytest.raises(OrganizerError, match="at most 3 levels"):
        await svc.create_folder("D", c.id)
    assert changes.count == 3
    assert len(svc.state.folders) == 4


async def test_move_respects_subtree_height(
    svc: OrganizerService, changes: _Counter
) -> None:
    a = await svc.create_folder("A")
    b = await svc.create_folder("B", a.id)
    x = await svc.create_folder("X")
    y = await svc.create_folder("Y", x.id)
    with pytest.raises(OrganizerError, match="at most 3 levels"):
        await svc.move_folder(x.id, b.id)
    await svc.move_folder(x.id, a.id)
    assert svc.state.folders[x.id].parent_id == a.id
    assert svc.state.folders[y.id].parent_id == x.id
    assert changes.count == 5


async def test_cycle_rejected(svc: OrganizerService, changes: _Counter) -> None:
    a = await svc.create_folder("A")
    b = await svc.create_folder("B", a.id)
    for target in (a.id, b.id):
        with pytest.raises(OrganizerError, match="into itself"):
            await svc.move_folder(a.id, target)
    with pytest.raises(OrganizerError):
        await svc.move_folder(ROOT_ID, a.id)
    assert changes.count == 2
    assert svc.state.folders[a.id].parent_id == ROOT_ID


async def test_duplicate_name_message(svc: OrganizerService, changes: _Counter) -> None:
    a = await svc.create_folder("Graphics")
    with pytest.raises(OrganizerError, match='A folder named "graphics" already'):
        await svc.create_folder("  graphics ")
    other = await svc.create_folder("Other")
    with pytest.raises(OrganizerError, match="already exists"):
        await svc.rename_folder(other.id, "GRAPHICS")
    await svc.create_folder("Graphics", other.id)
    await svc.rename_folder(a.id, "graphics")
    assert svc.state.folders[a.id].name == "graphics"
    assert changes.count == 4


async def test_empty_name_rejected(svc: OrganizerService, changes: _Counter) -> None:
    with pytest.raises(OrganizerError, match="cannot be empty"):
        await svc.create_folder("   ")
    assert changes.count == 0


async def test_create_folder_from_emits_once_and_rejects_duplicate(
    svc: OrganizerService, changes: _Counter
) -> None:
    initial_changes = changes.count
    f = await svc.create_folder_from("Core", ["mod.a", "mod.b"])
    assert changes.count == initial_changes + 1
    assert f.name == "Core"
    assert svc.state.placements["mod.a"] == f.id
    assert svc.state.placements["mod.b"] == f.id

    state_before = svc.state
    changes_before = changes.count

    with pytest.raises(OrganizerError):
        await svc.create_folder_from("core", ["mod.c"])

    assert changes.count == changes_before
    assert svc.state == state_before
    db_state = await svc._db.load()
    assert "mod.c" not in db_state.placements
    assert len(db_state.folders) == len(state_before.folders)


async def test_delete_folder_ungroups_mods(
    svc: OrganizerService, ctx: _FakeCtx, changes: _Counter
) -> None:
    ctx.add("u1", "One", "a.one")
    ctx.add("u2", "Two", "a.two")
    ctx.active_uuids = ["u1"]
    a = await svc.create_folder_from("A", ["A.One"])
    b = await svc.create_folder("B", a.id)
    await svc.place(["a.two"], b.id)
    await svc.set_rules([RuleSpec("package_id", "prefix", "a.", a.id)])
    assert changes.count == 4
    assert sorted(svc.folder_mod_uuids(a.id)) == ["u1", "u2"]

    await svc.delete_folder(a.id)

    assert changes.count == 5
    assert set(svc.state.folders) == {ROOT_ID}
    assert svc.state.rules == ()
    root = svc.tree()
    assert sorted(m.uuid for m in root.mods) == ["u1", "u2"]
    assert all(m.placement == "manual" for m in root.mods)
    assert (root.total, root.enabled, root.check) == (2, 1, "partial")


async def test_place_ungroup_and_reset(
    svc: OrganizerService, ctx: _FakeCtx, changes: _Counter
) -> None:
    ctx.add("u1", "One", "a.one")
    a = await svc.create_folder("A")
    await svc.set_rules([RuleSpec("name", "equals", " one ", a.id)])
    assert svc.state.rules[0].pattern == "one"
    assert svc.tree().children[0].mods[0].placement == "rule"

    await svc.ungroup(["a.one"])
    assert svc.tree().mods[0].placement == "manual"

    await svc.reset_to_rules(["a.one"])
    assert svc.tree().children[0].mods[0].placement == "rule"
    assert changes.count == 4


async def test_rules_validation(svc: OrganizerService, changes: _Counter) -> None:
    a = await svc.create_folder("A")
    with pytest.raises(OrganizerError, match="root"):
        await svc.set_rules([RuleSpec("name", "equals", "x", ROOT_ID)])
    with pytest.raises(OrganizerError, match="pattern cannot be empty"):
        await svc.set_rules([RuleSpec("name", "equals", "  ", a.id)])
    with pytest.raises(OrganizerError):
        await svc.set_rules([RuleSpec("name", "equals", "x", 999)])
    assert changes.count == 1


async def test_tags(svc: OrganizerService, changes: _Counter) -> None:
    tag = await svc.create_tag("QoL", "#aabbcc")
    with pytest.raises(OrganizerError, match="already exists"):
        await svc.create_tag("qol", "#000000")
    with pytest.raises(OrganizerError, match="not a valid color"):
        await svc.create_tag("Other", "red")
    await svc.set_mod_tags(["A.Mod"], add=[tag.id])
    assert svc.state.mod_tags["a.mod"] == frozenset({tag.id})
    with pytest.raises(OrganizerError):
        await svc.set_mod_tags(["a.mod"], add=[999])
    await svc.delete_tag(tag.id)
    assert svc.state.tags == {}
    assert not svc.state.mod_tags.get("a.mod")
    assert changes.count == 3


async def test_state_persists(tmp_path: Path, ctx: _FakeCtx) -> None:
    path = tmp_path / "organizer.db"
    first = OrganizerService(cast("CoreContext", ctx), OrganizerDb(path))
    await first.init(cast("CoreContext", ctx))
    folder = await first.create_folder("Kept")
    await first.shutdown()

    second = OrganizerService(cast("CoreContext", ctx), OrganizerDb(path))
    await second.init(cast("CoreContext", ctx))
    assert second.state.folders[folder.id].name == "Kept"
    await second.shutdown()


async def test_init_signals_readiness(tmp_path: Path, ctx: _FakeCtx) -> None:
    service = OrganizerService(
        cast("CoreContext", ctx), OrganizerDb(tmp_path / "organizer.db")
    )
    counter = _Counter()
    service.changed.connect(counter)
    assert not service.ready
    await service.init(cast("CoreContext", ctx))
    assert service.ready
    assert counter.count == 1
    await service.shutdown()


async def test_folder_toggle_enables_partial_and_disables_full(
    svc: OrganizerService, ctx: _FakeCtx
) -> None:
    ctx.add("u1", "One", "a.one")
    ctx.add("u2", "Two", "a.two")
    ctx.add("u3", "Loose", "loose.mod")
    a = await svc.create_folder_from("A", ["a.one"])
    b = await svc.create_folder("B", a.id)
    await svc.place(["a.two"], b.id)

    ctx.active_uuids = ["u1"]
    enable, disable = svc.folder_toggle(a.id)
    assert (sorted(enable), disable) == (["u1", "u2"], [])

    ctx.active_uuids = ["u1", "u2"]
    enable, disable = svc.folder_toggle(a.id)
    assert (enable, sorted(disable)) == ([], ["u1", "u2"])

    assert svc.folder_toggle(ROOT_ID, recursive=False) == (["u3"], [])
    ctx.active_uuids = ["u3"]
    assert svc.folder_toggle(ROOT_ID, recursive=False) == ([], ["u3"])
    assert svc.folder_mod_uuids(ROOT_ID, recursive=False) == ["u3"]


async def test_folder_move_targets_respect_subtree_depth_and_names(
    svc: OrganizerService,
) -> None:
    a = await svc.create_folder("A")
    b = await svc.create_folder("B", a.id)
    c = await svc.create_folder("C", b.id)
    x = await svc.create_folder("X")
    y = await svc.create_folder("Y", x.id)
    clash = await svc.create_folder("Clash")
    inner_x = await svc.create_folder("X", clash.id)

    assert svc.folder_move_targets(b.id) == {x.id, clash.id, ROOT_ID}
    assert svc.folder_move_targets(x.id) == {a.id}
    assert svc.folder_move_targets(c.id) == {
        a.id,
        x.id,
        y.id,
        clash.id,
        inner_x.id,
        ROOT_ID,
    }
    with pytest.raises(OrganizerError):
        svc.folder_move_targets(ROOT_ID)


async def test_tree_filters_count_status_and_providers(
    svc: OrganizerService, ctx: _FakeCtx
) -> None:
    ctx.add("u1", "One", "a.one")
    ctx.add("u2", "Two", "a.two")
    ctx.active_uuids = ["u1"]
    filters = {f.key: f for f in svc.tree_filters()}
    assert [(k, f.count) for k, f in filters.items()][:3] == [
        ("all", 2),
        ("active", 1),
        ("inactive", 1),
    ]
    provider = next(f for f in filters.values() if f.provider_id is not None)
    assert provider.count == 2
    assert provider.query("x").provider_ids == frozenset({provider.provider_id})
    assert filters["active"].query("one").status == "active"


async def test_tree_filters_with_persisted_tags_and_empty_tag(
    svc: OrganizerService, ctx: _FakeCtx
) -> None:
    ctx.add("u1", "One", "a.one")
    ctx.add("u2", "Two", "a.two")
    ctx.add("u3", "Three", "a.three")

    t1 = await svc.create_tag("Medieval", "#5eead4")
    t2 = await svc.create_tag("Combat", "#fb923c")
    t3 = await svc.create_tag("Empty", "#a3e635")

    await svc.set_mod_tags(["a.one"], add=[t1.id, t2.id])
    await svc.set_mod_tags(["a.two"], add=[t1.id])

    filters = {f.key: f for f in svc.tree_filters()}
    assert f"tag:{t1.id}" in filters
    assert filters[f"tag:{t1.id}"].count == 2
    assert filters[f"tag:{t1.id}"].label == "Medieval"
    assert filters[f"tag:{t1.id}"].tag_id == t1.id

    assert filters[f"tag:{t2.id}"].count == 1
    assert filters[f"tag:{t2.id}"].label == "Combat"

    assert filters[f"tag:{t3.id}"].count == 0
    assert filters[f"tag:{t3.id}"].label == "Empty"

    await svc.delete_tag(t2.id)
    after_del = {f.key: f for f in svc.tree_filters()}
    assert f"tag:{t2.id}" not in after_del
    assert f"tag:{t1.id}" in after_del


async def test_tree_filters_shared_package_ids_count_in_service(
    svc: OrganizerService, ctx: _FakeCtx
) -> None:
    ctx.add("u1", "Steam Copy", "shared.pkg")
    ctx.add("u2", "Local Copy", "shared.pkg")
    ctx.add("u3", "Other Mod", "other.pkg")

    tag = await svc.create_tag("Overhaul", "#5eead4")
    await svc.set_mod_tags(["shared.pkg"], add=[tag.id])

    filters = {f.key: f for f in svc.tree_filters()}
    assert filters[f"tag:{tag.id}"].count == 2


def _rule_rows(svc: OrganizerService) -> list[tuple[str, str, str, str]]:
    folders = svc.state.folders
    return [
        (folders[r.folder_id].name, r.field, r.op, r.pattern) for r in svc.state.rules
    ]


def _standard_rows() -> list[tuple[str, str, str, str]]:
    return [(r.folder, r.field, r.op, r.pattern) for r in STANDARD_RULES]


async def test_add_standard_rules_fresh_and_idempotent(
    svc: OrganizerService, changes: _Counter
) -> None:
    total = len(STANDARD_RULES)
    names = {r.folder for r in STANDARD_RULES}
    assert await svc.add_standard_rules() == total
    assert changes.count == 1
    assert _rule_rows(svc) == _standard_rows()
    top = [f for f in svc.state.folders.values() if f.parent_id == ROOT_ID]
    assert sorted(f.name for f in top) == sorted(names)
    assert len(svc.state.folders) == len(names) + 1
    assert [r.position for r in svc.state.rules] == list(range(total))

    assert await svc.add_standard_rules() == 0
    assert changes.count == 1
    assert len(svc.state.rules) == total
    assert len(svc.state.folders) == len(names) + 1


async def test_add_standard_rules_reuses_folder_and_keeps_user_rules(
    svc: OrganizerService, changes: _Counter
) -> None:
    official = await svc.create_folder("OFFICIAL")
    mine = await svc.create_folder("Mine")
    await svc.set_rules(
        [
            RuleSpec("author", "contains", "me", mine.id),
            RuleSpec("package_id", "prefix", "SARG.", mine.id),
        ]
    )
    changes.count = 0

    assert await svc.add_standard_rules() == len(STANDARD_RULES) - 1
    assert changes.count == 1
    names = [f.name for f in svc.state.folders.values()]
    assert "Official" not in names
    assert "Alpha Mods" not in names
    assert names.count("OFFICIAL") == 1
    rows = _rule_rows(svc)
    assert rows[:2] == [
        ("Mine", "author", "contains", "me"),
        ("Mine", "package_id", "prefix", "SARG."),
    ]
    expected = [row for row in _standard_rows() if row[3] != "sarg."]
    expected[0] = ("OFFICIAL", *expected[0][1:])
    assert rows[2:] == expected
    assert svc.state.rules[2].folder_id == official.id


async def test_standard_rules_resolve_mods(svc: OrganizerService) -> None:
    await svc.add_standard_rules()
    state = svc.state

    def folder(pid: str, name: str = "Mod", author: str = "") -> str:
        return state.folders[folder_for(state, pid, name, author)[0]].name

    assert folder("oskarpotocki.vfe.core") == "Vanilla Expanded"
    assert folder("someone.vfe", author="Oskar Potocki, Taranchuk") == (
        "Vanilla Expanded"
    )
    assert folder("ludeon.rimworld.biotech", "Biotech") == "Official"
    assert folder("brrainz.harmony", "Harmony") == "Frameworks & Libraries"
    assert folder("ceteam.combatextended", "Combat Extended") == "Combat Extended"
    assert folder("sarg.alphagenes") == "Alpha Mods"
    assert folder("bs.performance", "Performance Fish") == "Performance"
    assert folder("oskarpotocki.vanillafactionsexpanded.core") == (
        "Frameworks & Libraries"
    )
    assert folder("vanillaracesexpanded.sanguophage") == "Vanilla Expanded"
    assert folder("dubwise.dubsperformanceanalyzer.steam") == "Performance"
    assert folder("dubwise.rimatomics") == "Dubs Mods"
    assert folder("brrainz.achtung", "Achtung!") == "Quality of Life"
    assert folder("brrainz.jobsofopportunity") == "Root"
    assert folder("smartkar.athena.framework", "Athena Framework") == "Root"
    assert folder("someone.perf", "Performance Tweaks") == "Root"
    assert folder("someone.other", "Other") == "Root"


async def test_standard_package_id_rules_are_not_shadowed(
    svc: OrganizerService,
) -> None:
    await svc.add_standard_rules()
    state = svc.state
    for rule in STANDARD_RULES:
        if rule.field != "package_id":
            continue
        probe = rule.pattern if rule.op == "equals" else rule.pattern + "x"
        folder_id, _ = folder_for(state, probe, "Mod", "")
        assert state.folders[folder_id].name == rule.folder, rule.pattern
