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
from pxmodrim.core.organizer.models import ROOT_ID, OrganizerError, RuleSpec
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
