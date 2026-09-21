from __future__ import annotations

from collections.abc import Iterator
from types import SimpleNamespace
from typing import cast

import pytest
from PySide6.QtCore import QCoreApplication, QObject, QPoint, Qt, Signal
from PySide6.QtQml import QQmlEngine
from PySide6.QtTest import QTest
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
from pxmodrim.ui.components.svg_provider import create_qml_engine
from pxmodrim.ui.models.mod_list_model import ModListModel
from pxmodrim.ui.models.mod_list_proxy_model import ModListProxyModel
from pxmodrim.ui.panels import mod_list_panel
from pxmodrim.ui.panels.mod_list_panel import ModListPanel
from pxmodrim.ui.theme.qml_theme import Theme


@pytest.fixture(scope="module")
def qapp() -> Iterator[QApplication]:
    app = QCoreApplication.instance()
    if app is None:
        app = QApplication([])
    assert isinstance(app, QApplication)
    yield app


@pytest.fixture(scope="module")
def qml_engine(qapp: QApplication) -> Iterator[QQmlEngine]:
    engine = create_qml_engine()
    theme = Theme(engine)
    engine.rootContext().setContextProperty("Theme", theme)
    yield engine


class _Diagnostics(QObject):
    diagnostics_summary_changed = Signal(dict)

    def summary_for(self, uuid: str) -> None:
        return None


class _ModService(QObject):
    mods_changed = Signal()

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self.provider_colors: dict[str, str] = {}


class _WidgetContext(QObject):
    active_state_changed = Signal(object)

    def __init__(self, mods: dict[str, ListedMod]) -> None:
        super().__init__()
        self.all_mods = mods
        self.active_uuids: list[str] = []
        self.diagnostics_service = _Diagnostics(self)
        self.mod_service = _ModService(self)

    def set_active(self, uuids: list[str]) -> None:
        self.active_uuids = list(uuids)


def _widget_panel(qml_engine: QQmlEngine) -> ModListPanel:
    mods = {
        "uuid-a": _mod("Mod A", "mod.a"),
        "uuid-b": _mod("Mod B", "mod.b"),
        "uuid-c": _mod("Mod C", "mod.c"),
    }
    ctx = _WidgetContext(cast(dict[str, ListedMod], mods))
    panel = ModListPanel(cast(CoreContext, ctx), qml_engine)
    panel.load_mods(cast(dict[str, ListedMod], mods), [])
    return panel


def _qml_list_property(list_view: QObject, name: str) -> object:
    value = list_view.property(name)
    return value.toVariant() if hasattr(value, "toVariant") else value


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


def test_hovering_drag_handle_does_not_show_drag_proxy(
    qml_engine: QQmlEngine,
) -> None:
    panel = _widget_panel(qml_engine)
    panel.resize(420, 320)
    panel.show()
    QTest.qWait(20)
    root = panel._qml.rootObject()
    assert root is not None
    drag_proxy = root.findChild(QObject, "dragProxy")
    assert drag_proxy is not None

    QTest.mouseMove(panel._qml, QPoint(4, 26))
    QTest.mouseMove(panel._qml, QPoint(14, 26))

    assert drag_proxy.property("visible") is False


def test_qml_selection_clears_when_filter_hides_current_row(
    qml_engine: QQmlEngine,
) -> None:
    panel = _widget_panel(qml_engine)
    root = panel._qml.rootObject()
    assert root is not None
    list_view = root.findChild(QObject, "listView")
    assert list_view is not None
    assert list_view.property("currentIndex") == -1
    assert list_view.property("anchorIndex") == -1
    assert _qml_list_property(list_view, "selectedIndices") == []
    empty_state = root.findChild(QObject, "emptyStateText")
    assert empty_state is not None
    assert empty_state.property("visible") is False
    root.selectRow(2, "uuid-c", 0)  # type: ignore[attr-defined]
    panel.set_search_filter("Mod A")
    assert list_view.property("count") == 1
    assert list_view.property("currentIndex") == -1
    assert list_view.property("anchorIndex") == -1
    assert _qml_list_property(list_view, "selectedIndices") == []

    panel.set_search_filter("not present")
    assert list_view.property("count") == 0
    assert list_view.property("currentIndex") == -1
    assert list_view.property("anchorIndex") == -1
    assert _qml_list_property(list_view, "selectedIndices") == []
    assert empty_state.property("visible") is True


def test_qml_selection_follows_uuid_when_selected_row_moves(
    qml_engine: QQmlEngine,
) -> None:
    panel = _widget_panel(qml_engine)
    root = panel._qml.rootObject()
    assert root is not None
    list_view = root.findChild(QObject, "listView")
    assert list_view is not None

    root.selectRow(2, "uuid-c", 0)  # type: ignore[attr-defined]
    panel.moveRow(2, 0)

    assert panel.uuidAt(0) == "uuid-c"
    assert list_view.property("currentIndex") == 0
    assert _qml_list_property(list_view, "selectedIndices") == [0]
    assert _qml_list_property(list_view, "selectedUuids") == ["uuid-c"]


def test_search_focus_disables_list_keyboard_actions(
    qml_engine: QQmlEngine,
) -> None:
    panel = _widget_panel(qml_engine)
    panel.resize(420, 320)
    panel.show()
    QTest.qWait(20)
    root = panel._qml.rootObject()
    assert root is not None

    QTest.mouseClick(
        panel._qml,
        Qt.MouseButton.LeftButton,
        Qt.KeyboardModifier.NoModifier,
        QPoint(100, 26),
    )
    QTest.qWait(20)
    assert root.property("keyboardActive") is True
    panel.search_input.setFocus()
    QTest.qWait(10)
    assert root.property("keyboardActive") is False
    QTest.keyClick(panel.search_input, Qt.Key.Key_Return)
    assert panel.active_uuids() == []


@pytest.mark.asyncio
async def test_mod_selection_presenter_clears_on_empty_uuid() -> None:
    from unittest.mock import MagicMock

    from pxmodrim.ui.mod_selection import ModSelectionPresenter

    panel_mock = MagicMock()
    ctx = _WidgetContext({})
    presenter = ModSelectionPresenter(cast(CoreContext, ctx), panel_mock)
    await presenter.show("")
    panel_mock.clear.assert_called_once()
