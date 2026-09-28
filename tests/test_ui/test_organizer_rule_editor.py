from __future__ import annotations

import asyncio
from collections.abc import Callable, Iterator
from itertools import pairwise
from pathlib import Path

import pytest
from PySide6.QtCore import QCoreApplication, QObject, QPoint, QPointF, Qt
from PySide6.QtQml import QQmlEngine
from PySide6.QtQuick import QQuickItem
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QWidget

from pxmodrim.core.config import AppConfig, ConfigService
from pxmodrim.core.context import CoreContext
from pxmodrim.core.models.metadata.structures import AboutXmlMod, CaseInsensitiveStr
from pxmodrim.core.organizer import STANDARD_RULES, OrganizerDb, OrganizerService
from pxmodrim.ui.components import create_qml_engine
from pxmodrim.ui.context import AppContext
from pxmodrim.ui.plugins.organizer.view import OrganizerViewPanel
from pxmodrim.ui.theme.qml_theme import Theme


@pytest.fixture(scope="module")
def qapp() -> Iterator[QApplication]:
    app = QCoreApplication.instance()
    if app is None:
        app = QApplication([])
    assert isinstance(app, QApplication)
    yield app


async def _until(condition: Callable[[], bool], timeout: float = 3.0) -> None:
    """Pump both Qt and asyncio (asyncSlot tasks) until ``condition`` holds."""
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    while not condition():
        assert loop.time() < deadline, "condition not met before timeout"
        QTest.qWait(10)
        await asyncio.sleep(0)


def _click(widget: QWidget, item: QQuickItem) -> None:
    center = item.mapToScene(QPointF(item.width() / 2, item.height() / 2))
    QTest.mouseClick(
        widget,
        Qt.MouseButton.LeftButton,
        pos=QPoint(round(center.x()), round(center.y())),
    )


def _type(widget: QWidget, item: QQuickItem, value: str) -> None:
    _click(widget, item)
    QTest.keyClick(widget, Qt.Key.Key_A, Qt.KeyboardModifier.ControlModifier)
    QTest.keyClicks(widget, value)


def _find_quick_item(root_item: QQuickItem, name: str) -> QQuickItem | None:
    if root_item.objectName() == name:
        return root_item
    for child in root_item.childItems():
        found = _find_quick_item(child, name)
        if found is not None:
            return found
    return None


def _control(row: QQuickItem, name: str) -> QQuickItem:
    control = _find_quick_item(row, name)
    assert control is not None, f"Control {name} not found in row"
    return control


def _rows(dialog: QObject) -> list[QQuickItem]:
    repeater = dialog.findChild(QQuickItem, "organizerRuleRepeater")
    assert repeater is not None
    column = repeater.parentItem()
    assert column is not None
    rows = [i for i in column.childItems() if i.objectName() == "organizerRuleRow"]
    return sorted(rows, key=lambda item: item.y())


async def _laid_out_rows(dialog: QObject, count: int) -> list[QQuickItem]:
    """Wait until ``count`` rows exist and the layout has positioned them."""

    def settled() -> bool:
        rows = _rows(dialog)
        ys = [row.y() for row in rows]
        return len(rows) == count and all(a < b for a, b in pairwise(ys))

    await _until(settled)
    return _rows(dialog)


def _patterns(dialog: QObject) -> list[str]:
    return [
        str(_control(row, "organizerRulePattern").property("text"))
        for row in _rows(dialog)
    ]


def _hits(dialog: QObject, index: int) -> str:
    rows = _rows(dialog)
    if len(rows) <= index:
        return ""
    return str(_control(rows[index], "organizerRuleHits").property("text"))


@pytest.mark.asyncio
async def test_rule_editor_persists_ordered_rules_and_keeps_invalid_draft(
    tmp_path: Path, qapp: QApplication
) -> None:
    ctx = CoreContext.create(AppConfig(), ConfigService(tmp_path))
    ctx.load(
        {
            "example": AboutXmlMod(
                name="Example",
                _uuid="example",
                package_id=CaseInsensitiveStr("example.mod"),
                provider_id="local",
            )
        },
        [],
    )
    db = OrganizerDb(tmp_path / "organizer.db")
    service = OrganizerService(ctx, db)
    ctx.register_plugin(service)
    await service.init(ctx)
    first = await service.create_folder("First")
    second = await service.create_folder("Second")
    engine: QQmlEngine = create_qml_engine()
    engine.rootContext().setContextProperty("Theme", Theme(engine))
    parent = QWidget()
    view = OrganizerViewPanel(ctx, engine, parent, AppContext(ctx))
    parent.resize(920, 700)
    view.setGeometry(parent.rect())
    view.show()
    parent.show()
    # Earlier tests may leave another top-level active; key input needs ours.
    parent.activateWindow()
    assert QTest.qWaitForWindowActive(parent)
    view._rebuild()
    try:
        qml = view._qml
        root = qml.rootObject()
        assert root is not None
        dialog = root.findChild(QObject, "organizerRuleEditor")
        assert dialog is not None
        rules_button = view.rules_button

        async def open_editor(count: int) -> list[QQuickItem]:
            QTest.mouseClick(rules_button, Qt.MouseButton.LeftButton)
            await _until(lambda: bool(dialog.property("opened")))
            return await _laid_out_rows(dialog, count)

        async def save_and_close() -> None:
            assert save is not None
            _click(qml, save)
            await _until(lambda: not dialog.property("visible"))

        await open_editor(0)
        add = dialog.findChild(QQuickItem, "organizerAddRule")
        save = dialog.findChild(QQuickItem, "organizerSaveRules")
        assert add is not None and save is not None
        assert dialog.property("width") <= qml.width()

        # Add a rule, type its pattern, save.
        _click(qml, add)
        (row,) = await _laid_out_rows(dialog, 1)
        _type(qml, _control(row, "organizerRulePattern"), "example")
        assignable = dialog.findChild(QQuickItem, "organizerRuleAssignable")
        assert assignable is not None
        await _until(lambda: "1 mod matches first here" in _hits(dialog, 0))
        assert "1 currently ungrouped mod would" in str(assignable.property("text"))
        await save_and_close()

        persisted = await db.load()
        assert [(r.field, r.op, r.pattern, r.folder_id) for r in persisted.rules] == [
            ("package_id", "prefix", "example", first.id)
        ]
        assert service.folder_mod_uuids(first.id) == ["example"]

        # Add a second rule targeting the second folder, then move it up.
        await open_editor(1)
        _click(qml, add)
        _, new_row = await _laid_out_rows(dialog, 2)
        _type(qml, _control(new_row, "organizerRulePattern"), "example.mod")
        new_row = (await _laid_out_rows(dialog, 2))[1]
        operator_box = _control(new_row, "organizerRuleOperator")
        _click(qml, operator_box)
        await _until(lambda: bool(operator_box.property("down")))
        QTest.keyClick(qml, Qt.Key.Key_Down)
        QTest.keyClick(qml, Qt.Key.Key_Down)
        QTest.keyClick(qml, Qt.Key.Key_Return)
        await _until(lambda: operator_box.property("currentValue") == "equals")
        new_row = (await _laid_out_rows(dialog, 2))[1]
        folder_box = _control(new_row, "organizerRuleFolder")
        _click(qml, folder_box)
        await _until(lambda: bool(folder_box.property("down")))
        QTest.keyClick(qml, Qt.Key.Key_Down)
        QTest.keyClick(qml, Qt.Key.Key_Return)
        await _until(lambda: folder_box.property("currentValue") == second.id)

        _click(qml, _control(new_row, "organizerRuleUp"))
        await _until(lambda: _patterns(dialog) == ["example.mod", "example"])
        await _until(lambda: "1 mod matches first here" in _hits(dialog, 0))
        await _laid_out_rows(dialog, 2)
        await save_and_close()

        persisted = await db.load()
        assert [(r.op, r.pattern, r.folder_id) for r in persisted.rules] == [
            ("equals", "example.mod", second.id),
            ("prefix", "example", first.id),
        ]
        assert service.folder_mod_uuids(second.id) == ["example"]

        # An invalid pattern keeps the dialog and the unsaved draft visible.
        top, _ = await open_editor(2)
        _type(qml, _control(top, "organizerRulePattern"), "   ")
        _click(qml, save)
        await _until(
            lambda: "pattern cannot be empty" in str(dialog.property("errorMessage"))
        )
        assert dialog.property("visible")
        assert _patterns(dialog) == ["   ", "example"]
        assert len((await db.load()).rules) == 2

        # Remove the invalid rule from the kept draft and save the remainder.
        _click(qml, _control(top, "organizerRuleRemove"))
        await _laid_out_rows(dialog, 1)
        assert _patterns(dialog) == ["example"]
        await save_and_close()

        persisted = await db.load()
        assert [(r.pattern, r.folder_id) for r in persisted.rules] == [
            ("example", first.id)
        ]
        assert service.folder_mod_uuids(first.id) == ["example"]
    finally:
        view.close()
        parent.deleteLater()
        await service.shutdown()


@pytest.mark.asyncio
async def test_rule_editor_adds_standard_rules_only_from_clean_draft(
    tmp_path: Path, qapp: QApplication
) -> None:
    ctx = CoreContext.create(AppConfig(), ConfigService(tmp_path))
    ctx.load({}, [])
    db = OrganizerDb(tmp_path / "organizer.db")
    service = OrganizerService(ctx, db)
    ctx.register_plugin(service)
    await service.init(ctx)
    await service.create_folder("Mine")
    engine: QQmlEngine = create_qml_engine()
    engine.rootContext().setContextProperty("Theme", Theme(engine))
    parent = QWidget()
    view = OrganizerViewPanel(ctx, engine, parent, AppContext(ctx))
    parent.resize(920, 700)
    view.setGeometry(parent.rect())
    view.show()
    parent.show()
    parent.activateWindow()
    assert QTest.qWaitForWindowActive(parent)
    view._rebuild()
    try:
        qml = view._qml
        root = qml.rootObject()
        assert root is not None
        dialog = root.findChild(QObject, "organizerRuleEditor")
        assert dialog is not None
        QTest.mouseClick(view.rules_button, Qt.MouseButton.LeftButton)
        await _until(lambda: bool(dialog.property("opened")))
        await _laid_out_rows(dialog, 0)
        standard = dialog.findChild(QQuickItem, "organizerAddStandardRules")
        assert standard is not None
        assert standard.property("enabled")

        _click(qml, standard)
        rows = await _laid_out_rows(dialog, len(STANDARD_RULES))
        assert len(rows) == len(STANDARD_RULES)
        assert _patterns(dialog) == [rule.pattern for rule in STANDARD_RULES]
        assert dialog.property("visible")
        assert dialog.property("errorMessage") == ""

        persisted = await db.load()
        folders = {f.id: f.name for f in persisted.folders.values()}
        assert [
            (folders[r.folder_id], r.field, r.op, r.pattern) for r in persisted.rules
        ] == [(s.folder, s.field, s.op, s.pattern) for s in STANDARD_RULES]

        # A dirty draft must be saved or discarded before adding more rules.
        assert standard.property("enabled")
        _type(qml, _control(rows[0], "organizerRulePattern"), "edited")
        await _until(lambda: not standard.property("enabled"))
    finally:
        view.close()
        parent.deleteLater()
        await service.shutdown()
