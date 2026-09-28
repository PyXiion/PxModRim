from __future__ import annotations

from pathlib import Path
from typing import Any, cast

from PySide6.QtCore import QAbstractItemModel, QObject, QUrl, Signal
from PySide6.QtGui import QColor
from PySide6.QtQml import QQmlEngine
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import QWidget

from pxmodrim.ui.theme.palette import PALETTE

_QML = Path(__file__).parent / "FilterSidebar.qml"


class FilterSidebar(QQuickWidget):
    """Left-hand filter list shared by the Mods and Organizer views."""

    activated = Signal(int)

    def __init__(
        self,
        qml_engine: QQmlEngine | None,
        model: QAbstractItemModel,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(qml_engine, parent)  # pyright: ignore[reportCallIssue, reportArgumentType]
        self.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        self.setClearColor(QColor(PALETTE["ELEVATE_2"]))
        self.rootContext().setContextProperty("filterSidebarModel", model)
        self.setSource(QUrl.fromLocalFile(str(_QML)))
        root = self.rootObject()
        if root is not None:
            root.setProperty("model", model)
            cast(Any, root).activated.connect(self.activated)

    def current_index(self) -> int:
        root = self.rootObject()
        if root is None:
            return -1
        view = root.findChild(QObject, "listView")
        return int(view.property("currentIndex")) if view is not None else -1
