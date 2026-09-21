from __future__ import annotations

from collections.abc import Iterator
from types import SimpleNamespace
from typing import cast

import pytest
from PySide6.QtCore import QCoreApplication
from PySide6.QtWidgets import QApplication

from pxmodrim.core.checker.graph import ConstraintGraph, PackageId
from pxmodrim.core.context import CoreContext
from pxmodrim.core.models.metadata.structures import (
    AboutXmlMod,
    BaseRules,
    CaseInsensitiveStr,
    DependencyMod,
    ListedMod,
)
from pxmodrim.core.sort.config import SortSettings
from pxmodrim.ui.models.mod_list_model import ModListModel
from pxmodrim.ui.models.mod_list_proxy_model import ModListProxyModel
from pxmodrim.ui.panels import mod_list_panel
from pxmodrim.ui.panels.mod_list_panel import ModListPanel


@pytest.fixture(scope="module")
def qapp() -> Iterator[QApplication]:
    app = QCoreApplication.instance()
    if app is None:
        app = QApplication([])
    assert isinstance(app, QApplication)
    yield app


def _mod(name: str, package_id: str, dependency: str = "") -> AboutXmlMod:
    dependencies = {}
    if dependency:
        dependencies[CaseInsensitiveStr(dependency)] = DependencyMod(
            name=dependency,
            package_id=CaseInsensitiveStr(dependency),
        )
    return AboutXmlMod(
        name=name,
        package_id=CaseInsensitiveStr(package_id),
        provider_id="stub",
        about_rules=BaseRules(dependencies=dependencies),
    )


def _panel(qapp: QApplication) -> ModListPanel:
    mods = {
        "uuid-a": _mod("Mod A", "mod.a"),
        "uuid-b": _mod("Mod B", "mod.b", "mod.a"),
        "uuid-c": _mod("Mod C", "mod.c", "mod.b"),
    }
    graph = ConstraintGraph()
    graph.build(
        {PackageId(str(mod.package_id)): mod for mod in mods.values()},
        [PackageId("mod.a"), PackageId("mod.b"), PackageId("mod.c")],
        SortSettings(use_community_rules=False, use_alternative_package_ids=False),
    )
    ctx = SimpleNamespace(
        all_mods=mods,
        diagnostics_service=SimpleNamespace(constraint_graph=graph),
        active_uuids=["uuid-a", "uuid-b", "uuid-c"],
    )
    ctx.set_active = lambda uuids: setattr(ctx, "active_uuids", list(uuids))

    panel = ModListPanel.__new__(ModListPanel)
    panel._ctx = cast(CoreContext, ctx)
    panel._model = ModListModel({})
    panel._proxy = ModListProxyModel(panel._model)
    panel._model.load_mods(cast(dict[str, ListedMod], mods), ctx.active_uuids)
    return panel


@pytest.mark.asyncio
async def test_disabling_dependency_can_leave_dependents_active(
    qapp: QApplication, monkeypatch: pytest.MonkeyPatch
) -> None:
    panel = _panel(qapp)
    calls: list[list[str]] = []

    async def choose(*args: object) -> tuple[object, object]:
        calls.append(list(args[1]))  # type: ignore[arg-type]
        from PySide6.QtWidgets import QMessageBox

        return QMessageBox.StandardButton.No, None

    monkeypatch.setattr(mod_list_panel, "await_dialog", choose)
    await panel._toggle_rows([0])

    assert panel.active_uuids() == ["uuid-b", "uuid-c"]
    assert calls == [["Mod B (mod.b)", "Mod C (mod.c)"]]


@pytest.mark.asyncio
async def test_batch_disable_all_removes_transitive_dependents(
    qapp: QApplication, monkeypatch: pytest.MonkeyPatch
) -> None:
    panel = _panel(qapp)

    async def choose(*args: object) -> tuple[object, object]:
        from PySide6.QtWidgets import QMessageBox

        return QMessageBox.StandardButton.Yes, None

    monkeypatch.setattr(mod_list_panel, "await_dialog", choose)
    await panel._toggle_rows([0, 1])

    assert panel.active_uuids() == []


@pytest.mark.asyncio
async def test_cancel_keeps_selected_mod_and_dependents_active(
    qapp: QApplication, monkeypatch: pytest.MonkeyPatch
) -> None:
    panel = _panel(qapp)

    async def choose(*args: object) -> tuple[object, object]:
        from PySide6.QtWidgets import QMessageBox

        return QMessageBox.StandardButton.Cancel, None

    monkeypatch.setattr(mod_list_panel, "await_dialog", choose)
    await panel._toggle_rows([0])

    assert panel.active_uuids() == ["uuid-a", "uuid-b", "uuid-c"]
