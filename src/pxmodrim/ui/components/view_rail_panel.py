from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt, Signal, Slot
from PySide6.QtGui import QColor
from PySide6.QtQml import QQmlEngine
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import QWidget

from pxmodrim.ui.theme.palette import PALETTE

_QML_DIR = Path(__file__).parent
_RAIL_QML = _QML_DIR / "IconRail.qml"


class ViewRailPanel(QQuickWidget):
    currentChanged = Signal(int)
    hovered = Signal(int)

    def __init__(
        self,
        tabs: list[dict],
        qml_engine: QQmlEngine | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(qml_engine, parent)  # type: ignore[arg-type]
        self.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        self.setAttribute(Qt.WidgetAttribute.WA_AlwaysStackOnTop, False)
        self.setClearColor(QColor(PALETTE["ELEVATE_2"]))
        self.setMinimumWidth(48)
        self.setMaximumWidth(220)

        qml_ctx = self.rootContext()
        qml_ctx.setContextProperty("railPanel", self)
        qml_ctx.setContextProperty("railModel", tabs)
        self.setSource(str(_RAIL_QML))

        root = self.rootObject()
        if root is not None:
            root.tabSelected.connect(self._on_tab_selected)  # type: ignore[attr-defined]
            root.tabHovered.connect(self._on_tab_hovered)  # type: ignore[attr-defined]

    @Slot(int)
    def _on_tab_selected(self, index: int) -> None:
        self.currentChanged.emit(index)

    @Slot(int)
    def _on_tab_hovered(self, index: int) -> None:
        self.hovered.emit(index)

    def set_current(self, index: int) -> None:
        root = self.rootObject()
        if root is not None:
            root.setProperty("currentIndex", index)
