from __future__ import annotations

import asyncio
from pathlib import Path
from typing import Any, cast

import pytest
from PySide6.QtCore import Qt
from PySide6.QtQuick import QQuickItem
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QWidget

from pxmodrim.core.config import AppConfig, ConfigService
from pxmodrim.core.context import CoreContext
from pxmodrim.ui.components.svg_provider import create_qml_engine
from pxmodrim.ui.config import load_ui_prefs
from pxmodrim.ui.context import AppContext
from pxmodrim.ui.panels.settings_panel import SettingsPanel
from pxmodrim.ui.theme.qml_theme import Theme
from pxmodrim.ui.ui_prefs import UIPrefs


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
    panel = SettingsPanel(AppContext(CoreContext(AppConfig())), engine, host)
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


@pytest.mark.parametrize("save", [False, True])
def test_interface_scale_persists_only_on_save(
    qapp: QApplication, tmp_path: Path, save: bool
) -> None:
    service = ConfigService(tmp_path)
    app_ctx = AppContext(CoreContext(AppConfig(), service), UIPrefs())
    host = QWidget()
    engine = create_qml_engine(host)
    engine.rootContext().setContextProperty("Theme", Theme(engine))
    panel = SettingsPanel(app_ctx, engine, host)
    root = panel._qml.rootObject()
    assert isinstance(root, QQuickItem)
    control = root.findChild(QQuickItem, "interfaceScale")
    assert control is not None
    assert control.property("currentValue") == 100
    control.setProperty("currentIndex", 2)
    assert app_ctx.ui_prefs.ui_scale == 100
    assert not (tmp_path / "ui_prefs.json").exists()

    if save:
        initial = cast("dict[str, Any]", panel._backend.initial)
        panel._backend.save({**initial, "uiScale": control.property("currentValue")})
    else:
        panel._backend.cancel()

    expected = 150 if save else 100
    assert app_ctx.ui_prefs.ui_scale == expected
    assert load_ui_prefs(service).ui_scale == expected
    assert (tmp_path / "ui_prefs.json").exists() is save
    panel.deleteLater()
    host.deleteLater()
