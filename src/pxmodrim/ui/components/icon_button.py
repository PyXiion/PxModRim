from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QPushButton, QWidget

from pxmodrim.ui.components.icons import icon


class IconButton(QPushButton):
    def __init__(
        self,
        icon_name: str,
        tooltip: str = "",
        primary: bool = False,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._icon_name = icon_name
        self._primary = primary

        self.setIcon(icon(icon_name, 16, "#949ba4"))
        self.setFixedSize(32, 32)
        self.setObjectName("iconBtn")
        if primary:
            self.setProperty("primary", True)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setToolTip(tooltip)

    def set_icon_color(self, color: str) -> None:
        self.setIcon(icon(self._icon_name, 16, color))
