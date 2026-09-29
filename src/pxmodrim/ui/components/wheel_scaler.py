from __future__ import annotations

from PySide6.QtCore import QEvent, QObject, QPoint, Qt
from PySide6.QtGui import QWheelEvent
from PySide6.QtWidgets import QApplication

DEFAULT_WHEEL_FACTOR = 2.5


def _scaled(point: QPoint, factor: float) -> QPoint:
    return QPoint(round(point.x() * factor), round(point.y() * factor))


class WheelScaler(QObject):
    """Multiplies wheel/touchpad deltas for every widget and QML surface.

    Qt's default step (3 lines, ~60px per notch) feels slow in list-heavy views,
    and Qt Quick offers no global knob, so the events are rescaled at the source.
    """

    def __init__(self, app: QApplication, factor: float = DEFAULT_WHEEL_FACTOR) -> None:
        super().__init__(app)
        self._factor = factor
        self._forwarding = False
        app.installEventFilter(self)

    def eventFilter(self, watched: QObject, event: QEvent) -> bool:
        if (
            self._forwarding
            or event.type() != QEvent.Type.Wheel
            or not isinstance(event, QWheelEvent)
            # Ctrl+wheel is zoom, not scrolling.
            or event.modifiers() & Qt.KeyboardModifier.ControlModifier
        ):
            return False
        scaled = QWheelEvent(
            event.position(),
            event.globalPosition(),
            _scaled(event.pixelDelta(), self._factor),
            _scaled(event.angleDelta(), self._factor),
            event.buttons(),
            event.modifiers(),
            event.phase(),
            event.inverted(),
            event.source(),
        )
        self._forwarding = True
        try:
            QApplication.sendEvent(watched, scaled)
        finally:
            self._forwarding = False
        return True
