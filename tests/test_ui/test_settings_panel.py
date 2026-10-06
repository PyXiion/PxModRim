from __future__ import annotations

import asyncio
from typing import Any

from PySide6.QtCore import Qt
from PySide6.QtQuick import QQuickItem
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QWidget

from pxmodrim.core.config import AppConfig
from pxmodrim.core.context import CoreContext
from pxmodrim.ui.components.svg_provider import create_qml_engine
from pxmodrim.ui.panels.settings_panel import SettingsPanel
from pxmodrim.ui.theme.qml_theme import Theme


def _button(root: QQuickItem, text: str) -> QQuickItem | None:
    for child in root.findChildren(QQuickItem):
        if child.metaObject().className().startswith("PxButton") and (
            child.property("text") == text
        ):
            return child
    return None


async def test_cancel_click_closes_settings_and_unloads_without_errors(
    qapp: QApplication,
) -> None:
    host = QWidget()
    engine = create_qml_engine(host)
    engine.rootContext().setContextProperty("Theme", Theme(engine))
    warnings: list[Any] = []
    engine.warnings.connect(warnings.extend)
    panel = SettingsPanel(CoreContext(AppConfig()), engine, host)
    panel.resize(760, 640)
    panel.show()
    await asyncio.sleep(0.3)
    root = panel._qml.rootObject()
    assert isinstance(root, QQuickItem)
    cancel = _button(root, "Cancel")
    assert cancel is not None
    center = cancel.mapToScene(cancel.boundingRect().center()).toPoint()

    QTest.mouseClick(panel._qml, Qt.MouseButton.LeftButton, pos=center)
    QTest.qWait(20)
    assert panel._qml.rootObject() is None
    assert not panel.isVisible()
    panel.deleteLater()
    await asyncio.sleep(0.05)
    assert [w.toString() for w in warnings] == []
    host.deleteLater()
