from __future__ import annotations

from PySide6.QtGui import QColor
from PySide6.QtQml import QQmlEngine
from PySide6.QtWidgets import QVBoxLayout, QWidget

from pxmodrim.core.context import CoreContext
from pxmodrim.ui.theme.palette import PALETTE


class BaseViewPanel(QWidget):
    """UI-side whole-window view contributed to the left icon rail.

    Core never imports this; views are discovered entirely within ``ui/``.
    Subclasses must define ``view_id``, ``icon_name`` and ``label``.
    """

    view_id: str
    icon_name: str
    label: str

    def __init__(
        self,
        ctx: CoreContext,
        qml_engine: QQmlEngine | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._ctx = ctx
        self._qml_engine = qml_engine
        self.setAutoFillBackground(True)
        self._root = QVBoxLayout(self)
        self._root.setContentsMargins(0, 0, 0, 0)
        self._root.setSpacing(0)
        self.set_clear_color()

    def set_clear_color(self, color: str = PALETTE["ELEVATE_0"]) -> None:
        from PySide6.QtGui import QPalette

        palette = self.palette()
        palette.setColor(QPalette.ColorRole.Window, QColor(color))
        self.setPalette(palette)
