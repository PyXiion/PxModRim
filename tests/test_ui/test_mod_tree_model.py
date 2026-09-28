from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator, Iterator
from pathlib import Path
from types import SimpleNamespace
from typing import cast

import pytest
from PySide6.QtCore import QCoreApplication, QMetaObject, QObject, QPoint, QPointF, Qt
from PySide6.QtQml import QQmlEngine
from PySide6.QtQuick import QQuickItem
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QWidget
from pytestqt.qtbot import QtBot

from pxmodrim.core.config import AppConfig, ConfigService
from pxmodrim.core.context import CoreContext
from pxmodrim.core.models.metadata.structures import (
    AboutXmlMod,
    CaseInsensitiveStr,
    ListedMod,
)
from pxmodrim.core.models.view.diagnostics import ModDiagnosticsView
from pxmodrim.core.organizer import ROOT_ID, OrganizerDb, OrganizerService, TreeFilter
from pxmodrim.ui.components import create_qml_engine
from pxmodrim.ui.components.button import AppButton
from pxmodrim.ui.context import AppContext
from pxmodrim.ui.plugins.organizer.filter_model import OrganizerFilterModel
from pxmodrim.ui.plugins.organizer.tree_model import ModTreeModel
from pxmodrim.ui.plugins.organizer.view import OrganizerViewPanel
from pxmodrim.ui.theme.qml_theme import Theme
from pxmodrim.ui.views.mods_view import ModsViewPanel


@pytest.fixture(scope="module")
def qapp() -> Iterator[QApplication]:
    app = QCoreApplication.instance()
    if app is None:
        app = QApplication([])
    assert isinstance(app, QApplication)
    yield app


@pytest.fixture
async def organizer(
    tmp_path: Path,
) -> AsyncIterator[tuple[OrganizerService, SimpleNamespace]]:
    mods: dict[str, ListedMod] = {
        "u1": AboutXmlMod(
            name="One",
            _uuid="u1",
            package_id=CaseInsensitiveStr("a.one"),
            provider_id="local",
        ),
        "u2": AboutXmlMod(
            name="Two",
            _uuid="u2",
            package_id=CaseInsensitiveStr("a.two"),
            provider_id="steam",
        ),
        "u3": AboutXmlMod(
            name="Loose",
            _uuid="u3",
            package_id=CaseInsensitiveStr("loose.mod"),
            provider_id="local",
        ),
    }
    ctx = SimpleNamespace(
        all_mods=mods,
        active_uuids=["u1"],
        diagnostics_service=SimpleNamespace(summary_for=lambda _uuid: None),
    )
    svc = OrganizerService(
        cast(CoreContext, ctx), OrganizerDb(tmp_path / "organizer.db")
    )
    await svc.init(cast(CoreContext, ctx))
    yield svc, ctx
    await svc.shutdown()


async def test_nested_folder_rows_and_ungrouped(
    organizer: tuple[OrganizerService, SimpleNamespace], qapp: QApplication
) -> None:
    svc, ctx = organizer
    top = await svc.create_folder("Top")
    sub = await svc.create_folder("Sub", top.id)
    await svc.place(["a.one"], top.id)
    await svc.place(["a.two"], sub.id)
    model = ModTreeModel(lambda _uuid: None)
    model.set_tree(svc.tree(), ctx.all_mods, {}, False)

    assert model.rowCount() == 2
    root_folder = model.index(0, 0)
    ungrouped = model.index(1, 0)
    assert model.data(root_folder) == "Top"
    assert model.data(ungrouped) == "Ungrouped"
    assert model.data(model.index(0, 0, root_folder)) == "Sub"
    assert model.data(model.index(1, 0, root_folder)) == "One"
    assert model.data(model.index(0, 0, ungrouped)) == "Loose"
    assert model.parent(model.index(0, 0, ungrouped)) == ungrouped
    assert model.parent(model.index(0, 0, root_folder)) == root_folder
    assert model.parent(root_folder).isValid() is False
    assert model.data(ungrouped, model.TotalCountRole) == 1
    assert model.data(root_folder, model.TotalCountRole) == 2


async def test_check_states_and_filtered_counts(
    organizer: tuple[OrganizerService, SimpleNamespace], qapp: QApplication
) -> None:
    svc, ctx = organizer
    folder = await svc.create_folder_from("Group", ["a.one", "a.two"])
    model = ModTreeModel(lambda _uuid: None)
    model.set_tree(svc.tree(), ctx.all_mods, {}, False)
    idx = model.index(0, 0)
    assert model.data(idx, model.CheckStateRole) == Qt.CheckState.PartiallyChecked
    assert (
        model.data(model.index(0, 0, idx), model.CheckStateRole)
        == Qt.CheckState.Checked
    )
    assert (
        model.data(model.index(1, 0, idx), model.CheckStateRole)
        == Qt.CheckState.Unchecked
    )
    assert (
        model.data(model.index(1, 0), model.CheckStateRole) == Qt.CheckState.Unchecked
    )

    from pxmodrim.core.organizer import TreeQuery

    model.set_tree(svc.tree(TreeQuery(text="One")), ctx.all_mods, {}, True)
    idx = model.index(0, 0)
    assert model.data(idx, model.VisibleCountRole) == 1
    assert model.data(idx, model.TotalCountRole) == 2
    assert model.data(idx, model.CheckStateRole) == Qt.CheckState.PartiallyChecked
    assert model.index_for_key(f"f:{folder.id}").isValid()


async def test_folder_toggle_applies_once_for_descendants(
    organizer: tuple[OrganizerService, SimpleNamespace], qapp: QApplication
) -> None:
    svc, ctx = organizer
    top = await svc.create_folder("Top")
    sub = await svc.create_folder("Sub", top.id)
    await svc.place(["a.one"], top.id)
    await svc.place(["a.two"], sub.id)
    calls: list[tuple[list[str], list[str]]] = []

    class Activation:
        def dependents_of(self, uuids: list[str]) -> list[str]:
            return []

        def apply(self, enable: list[str], disable: list[str]) -> bool:
            calls.append((enable, disable))
            return True

    ctx.activation = Activation()
    view = OrganizerViewPanel.__new__(OrganizerViewPanel)
    view._ctx = cast(CoreContext, ctx)
    view._service = svc
    view.model = ModTreeModel(lambda _uuid: None)
    view.model.set_tree(svc.tree(), ctx.all_mods, {}, False)
    node = view.model.node_at(view.model.index_for_key(f"f:{top.id}"))
    assert node is not None
    await view._toggle_folder(node)
    assert calls == [(["u1", "u2"], [])]
    assert svc.folder_mod_uuids(ROOT_ID, recursive=False) == ["u3"]


async def test_issue_markers_only_show_on_enabled_mods(
    organizer: tuple[OrganizerService, SimpleNamespace], qapp: QApplication
) -> None:
    svc, ctx = organizer
    summary = ModDiagnosticsView(
        has_errors=True,
        has_warnings=True,
        error_tooltip="Error",
        warning_tooltip="Incompatible",
    )
    model = ModTreeModel(lambda _uuid: summary)
    model.set_tree(svc.tree(), ctx.all_mods, {}, False)
    ungrouped = model.index(0, 0)
    enabled = model.index_for_key("m:u1")
    inactive = model.index_for_key("m:u2")
    assert model.parent(enabled) == ungrouped
    assert model.data(enabled, model.HasErrorRole) is True
    assert model.data(enabled, model.HasWarningRole) is True
    assert model.data(inactive, model.HasErrorRole) is False
    assert model.data(inactive, model.HasWarningRole) is False

    ctx.active_uuids.append("u2")
    model.set_tree(svc.tree(), ctx.all_mods, {}, False)
    assert model.data(model.index_for_key("m:u2"), model.HasWarningRole) is True


@pytest.mark.asyncio
async def test_qml_tree_click_passes_model_index_to_panel(
    tmp_path: Path, qapp: QApplication, qtbot: QtBot
) -> None:
    ctx = CoreContext.create(AppConfig(), ConfigService(tmp_path))
    mod = AboutXmlMod(
        name="Example",
        _uuid="example",
        package_id=CaseInsensitiveStr("example.mod"),
        provider_id="local",
    )
    ctx.load({"example": mod}, [])
    service = OrganizerService(ctx, OrganizerDb(tmp_path / "organizer.db"))
    ctx.register_plugin(service)
    await service.init(ctx)
    engine: QQmlEngine = create_qml_engine()
    engine.rootContext().setContextProperty("Theme", Theme(engine))
    parent = QWidget()
    view = OrganizerViewPanel(ctx, engine, parent, AppContext(ctx))
    parent.resize(900, 600)
    view.resize(900, 600)
    parent.show()
    view.show()
    view._rebuild()
    try:
        tree = view._qml.rootObject().findChild(QObject, "organizerTreeView")
        assert tree is not None
        assert view.model.rowCount() == 1
        qtbot.waitUntil(lambda: tree.property("contentHeight") > 0)
        QTest.mouseClick(view._qml, Qt.MouseButton.LeftButton, pos=QPoint(200, 20))
        assert [node.kind for node in view.model.selected_nodes()] == ["ungrouped"]
    finally:
        view.close()
        parent.deleteLater()
        await service.shutdown()


def _click_quick(widget: QWidget, item: QQuickItem) -> None:
    center = item.mapToScene(QPointF(item.width() / 2, item.height() / 2))
    QTest.mouseClick(
        widget,
        Qt.MouseButton.LeftButton,
        pos=QPoint(round(center.x()), round(center.y())),
    )


def _find_quick_item(root_item: QQuickItem, name: str) -> QQuickItem | None:
    if root_item.objectName() == name:
        return root_item
    for child in root_item.childItems():
        found = _find_quick_item(child, name)
        if found is not None:
            return found
    return None


@pytest.mark.asyncio
async def test_qml_tag_editor_persists_assignments_and_edits(
    tmp_path: Path, qapp: QApplication, qtbot: QtBot
) -> None:
    ctx = CoreContext.create(AppConfig(), ConfigService(tmp_path))
    mods: dict[str, ListedMod] = {
        uuid: AboutXmlMod(
            name=name,
            _uuid=uuid,
            package_id=CaseInsensitiveStr(package_id),
            provider_id="local",
        )
        for uuid, name, package_id in (
            ("example", "Example", "example.mod"),
            ("second", "Second", "second.mod"),
        )
    }
    ctx.load(mods, [])
    db = OrganizerDb(tmp_path / "organizer.db")
    service = OrganizerService(ctx, db)
    ctx.register_plugin(service)
    await service.init(ctx)
    engine = create_qml_engine()
    engine.rootContext().setContextProperty("Theme", Theme(engine))
    parent = QWidget()
    view = OrganizerViewPanel(ctx, engine, parent, AppContext(ctx))
    parent.resize(1400, 850)
    view.setGeometry(parent.rect())
    parent.show()
    view.show()
    view._rebuild()
    try:
        root = view._qml.rootObject()
        assert root is not None
        tags = root.findChild(QObject, "organizerTagEditor")
        assert tags is not None
        selected = view.model.index_for_key("m:example")
        assert selected.isValid()
        view.model.select((selected,))
        view._sync_info()
        await view.selectionAction("tags")
        qtbot.waitUntil(lambda: tags.property("visible"))
        name = tags.findChild(QQuickItem, "organizerNewTagName")
        color = tags.findChild(QQuickItem, "organizerNewTagColor")
        add_tag = tags.findChild(QQuickItem, "organizerAddTag")
        error = tags.findChild(QQuickItem, "organizerTagError")
        assert name is not None and color is not None and add_tag is not None
        assert error is not None
        name.setProperty("text", "Favorites")
        color.setProperty("text", "not-a-color")
        _click_quick(view._qml, add_tag)
        await asyncio.sleep(0.03)
        assert "valid color" in error.property("text")
        assert name.property("text") == "Favorites"
        assert color.property("text") == "not-a-color"
        assert not service.state.tags

        color.setProperty("text", "#3399aa")
        _click_quick(view._qml, add_tag)
        await asyncio.sleep(0.03)
        assert [(tag.name, tag.color) for tag in service.state.tags.values()] == [
            ("Favorites", "#3399aa")
        ]
        assert error.property("text") == ""

        first = view.model.index_for_key("m:example")
        second = view.model.index_for_key("m:second")
        assert first.isValid() and second.isValid()
        view.model.select((first,))
        view._sync_info()
        await asyncio.sleep(0.03)
        QTest.qWait(10)
        repeater = tags.findChild(QQuickItem, "organizerTagRepeater")
        assert repeater is not None
        repeater_parent = repeater.parentItem()
        assert repeater_parent is not None
        assignment = _find_quick_item(repeater_parent, "organizerTagAssignment")
        assert assignment is not None and assignment.isVisible()
        assert assignment.property("checkState") == Qt.CheckState.Unchecked
        _click_quick(view._qml, assignment)
        await asyncio.sleep(0.03)
        tag_id = next(iter(service.state.tags))
        assert service.state.mod_tags["example.mod"] == frozenset({tag_id})
        assert assignment.property("checkState") == Qt.CheckState.Checked

        view.model.select((first, second))
        view._sync_info()
        await asyncio.sleep(0.03)
        QTest.qWait(10)
        assert assignment.property("checkState") == Qt.CheckState.PartiallyChecked
        _click_quick(view._qml, assignment)
        await asyncio.sleep(0.03)
        assert service.state.mod_tags["second.mod"] == frozenset({tag_id})
        assert assignment.property("checkState") == Qt.CheckState.Checked
        _click_quick(view._qml, assignment)
        await asyncio.sleep(0.03)
        assert not service.state.mod_tags.get("example.mod")
        assert not service.state.mod_tags.get("second.mod")
        assert assignment.property("checkState") == Qt.CheckState.Unchecked

        edit = _find_quick_item(repeater_parent, "organizerEditTag")
        tag_name = _find_quick_item(repeater_parent, "organizerTagName")
        tag_color = _find_quick_item(repeater_parent, "organizerTagColor")
        assert edit is not None and tag_name is not None and tag_color is not None
        _click_quick(view._qml, edit)
        assert not tag_name.property("readOnly")
        tag_name.setProperty("text", "Best")
        tag_color.setProperty("text", "bad")
        _click_quick(view._qml, edit)
        await asyncio.sleep(0.03)
        assert "valid color" in error.property("text")
        assert tag_name.property("text") == "Best"
        assert tag_color.property("text") == "bad"
        assert not tag_name.property("readOnly")
        assert service.state.tags[tag_id].name == "Favorites"

        tag_color.setProperty("text", "#123456")
        _click_quick(view._qml, edit)
        await asyncio.sleep(0.03)
        assert (service.state.tags[tag_id].name, service.state.tags[tag_id].color) == (
            "Best",
            "#123456",
        )
        assert tag_name.property("readOnly")
        assert error.property("text") == ""
        persisted = await db.load()
        assert (persisted.tags[tag_id].name, persisted.tags[tag_id].color) == (
            "Best",
            "#123456",
        )
        assert not persisted.mod_tags.get("example.mod")
        assert not persisted.mod_tags.get("second.mod")

        delete_btn = _find_quick_item(repeater_parent, "organizerDeleteTag")
        assert delete_btn is not None
        _click_quick(view._qml, delete_btn)
        delete_confirm = tags.findChild(QObject, "organizerDeleteTagConfirm")
        assert delete_confirm is not None
        qtbot.waitUntil(lambda: delete_confirm.property("visible"))
        QMetaObject.invokeMethod(delete_confirm, "accept")
        await asyncio.sleep(0.03)
        assert not service.state.tags
        persisted = await db.load()
        assert not persisted.tags
    finally:
        view.close()
        parent.deleteLater()
        await service.shutdown()


def test_organizer_filter_model_sections_icons_and_fallbacks(
    qapp: QApplication,
) -> None:
    model = OrganizerFilterModel()
    assert model.rowCount() == 0
    assert model.for_key("missing").key == "all"

    entries = [
        TreeFilter("all", "All", 10),
        TreeFilter("active", "Active", 6, status="active"),
        TreeFilter("inactive", "Inactive", 4, status="inactive"),
        TreeFilter("provider:steam", "Steam", 8, provider_id="steam"),
        TreeFilter("tag:1", "Medieval", 5, tag_id=1),
        TreeFilter("tag:2", "Empty", 0, tag_id=2),
    ]
    model.update(entries)
    assert model.rowCount() == 6

    # SectionRole checks
    assert [
        model.data(model.index(i), OrganizerFilterModel.SectionRole) for i in range(6)
    ] == ["Status", "Status", "Status", "Providers", "Tags", "Tags"]

    # IconRole checks
    assert [
        model.data(model.index(i), OrganizerFilterModel.IconRole) for i in range(6)
    ] == ["grid", "check-circle", "ban", "steam", "tag", "tag"]

    # Key and Display role checks
    assert model.data(model.index(4), OrganizerFilterModel.KeyRole) == "tag:1"
    assert model.data(model.index(4), Qt.ItemDataRole.DisplayRole) == "Medieval"
    assert model.data(model.index(4), OrganizerFilterModel.CountRole) == 5
    assert model.data(model.index(5), OrganizerFilterModel.CountRole) == 0

    # for_key lookup and fallback for deleted/unknown tag
    assert model.for_key("tag:1").label == "Medieval"
    assert model.for_key("tag:999").key == "all"

    # In-place update (label rename without key change)
    renamed_entries = [
        TreeFilter("all", "All", 10),
        TreeFilter("active", "Active", 6, status="active"),
        TreeFilter("inactive", "Inactive", 4, status="inactive"),
        TreeFilter("provider:steam", "Steam", 8, provider_id="steam"),
        TreeFilter("tag:1", "Medieval Overhaul", 5, tag_id=1),
        TreeFilter("tag:2", "Empty", 0, tag_id=2),
    ]
    model.update(renamed_entries)
    label = model.data(model.index(4), Qt.ItemDataRole.DisplayRole)
    assert label == "Medieval Overhaul"


@pytest.mark.asyncio
async def test_organizer_sidebar_geometry_matches_mods_after_dismissing_hint(
    tmp_path: Path, qapp: QApplication
) -> None:
    ctx = CoreContext.create(AppConfig(), ConfigService(tmp_path))
    ctx.load({}, [])
    service = OrganizerService(ctx, OrganizerDb(tmp_path / "organizer.db"))
    ctx.register_plugin(service)
    await service.init(ctx)
    engine = create_qml_engine()
    engine.rootContext().setContextProperty("Theme", Theme(engine))
    parent = QWidget()
    parent.resize(1300, 780)
    organizer = OrganizerViewPanel(ctx, engine, parent, AppContext(ctx))
    mods = ModsViewPanel(ctx, engine, parent, AppContext(ctx))
    organizer.setGeometry(parent.rect())
    mods.setGeometry(parent.rect())
    parent.show()
    organizer.show()
    mods.show()
    try:
        QTest.qWait(10)
        assert organizer._sidebar.width() == mods.sidebar.width()
        assert (
            organizer._sidebar.rootObject().childItems()[0].width()
            == mods.sidebar._qml.width()
        )
        assert organizer.mod_info.width() == mods.mod_info.width()

        mods.hide()
        hint = organizer.findChild(QWidget, "organizerHint")
        assert hint is not None
        dismiss = hint.findChild(AppButton)
        assert dismiss is not None
        width = organizer.mod_info.width()
        dismiss.click()
        QTest.qWait(10)
        assert not hint.isVisible()
        assert organizer.mod_info.width() == width
    finally:
        organizer.close()
        mods.close()
        parent.deleteLater()
        await service.shutdown()
