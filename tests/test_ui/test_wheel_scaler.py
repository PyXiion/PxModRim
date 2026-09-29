from __future__ import annotations

from PySide6.QtCore import QPoint, QPointF, Qt
from PySide6.QtGui import QWheelEvent
from PySide6.QtWidgets import QApplication, QListWidget
from pytestqt.qtbot import QtBot

from pxmodrim.ui.components.wheel_scaler import WheelScaler


def _wheel(view: QListWidget, modifiers: Qt.KeyboardModifier) -> None:
    pos = QPointF(10, 10)
    event = QWheelEvent(
        pos,
        view.mapToGlobal(pos),
        QPoint(0, 0),
        QPoint(0, -120),
        Qt.MouseButton.NoButton,
        modifiers,
        Qt.ScrollPhase.NoScrollPhase,
        False,
    )
    QApplication.sendEvent(view.viewport(), event)


def _scroll_after_wheel(
    qtbot: QtBot, factor: float | None, modifiers: Qt.KeyboardModifier
) -> int:
    view = QListWidget()
    qtbot.addWidget(view)
    view.addItems([str(i) for i in range(500)])
    view.resize(200, 200)
    view.show()
    app = QApplication.instance()
    assert isinstance(app, QApplication)
    scaler = WheelScaler(app, factor) if factor is not None else None
    try:
        _wheel(view, modifiers)
        return view.verticalScrollBar().value()
    finally:
        if scaler is not None:
            scaler.deleteLater()
            app.removeEventFilter(scaler)


def test_wheel_scrolls_further_with_scaler(qtbot: QtBot) -> None:
    base = _scroll_after_wheel(qtbot, None, Qt.KeyboardModifier.NoModifier)
    fast = _scroll_after_wheel(qtbot, 3.0, Qt.KeyboardModifier.NoModifier)
    assert base > 0
    assert fast == base * 3


def test_ctrl_wheel_is_left_untouched(qtbot: QtBot) -> None:
    base = _scroll_after_wheel(qtbot, None, Qt.KeyboardModifier.ControlModifier)
    fast = _scroll_after_wheel(qtbot, 3.0, Qt.KeyboardModifier.ControlModifier)
    assert fast == base
