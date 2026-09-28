from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QPushButton, QWidget

from pxmodrim.ui.components.icons import icon
from pxmodrim.ui.theme.palette import PALETTE


class IconButton(QPushButton):
    def __init__(
        self,
        icon_name: str,
        tooltip: str = "",
        primary: bool = False,
        size: int = 32,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._icon_name = icon_name
        self._primary = primary
        self._icon_px = max(16, size // 2)

        self.set_icon_color(PALETTE["TEXT_MUTED"])
        self.setFixedSize(size, size)
        self.setObjectName("iconBtn")
        if primary:
            self.setProperty("primary", True)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setToolTip(tooltip)

    def set_icon_color(self, color: str) -> None:
        self.setIcon(icon(self._icon_name, self._icon_px, color))
