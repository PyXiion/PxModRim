from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor
from PySide6.QtQml import QQmlEngine
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import QWidget

from pxmodrim.ui.components.header_controller import HeaderController
from pxmodrim.ui.palette import PALETTE

_QML_DIR = Path(__file__).parent.parent / "qml"
_HEADER_QML = _QML_DIR / "Header.qml"


class HeaderPanel(QQuickWidget):
    def __init__(
        self,
        controller: HeaderController,
        qml_engine: QQmlEngine | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(qml_engine, parent)  # type: ignore[arg-type]
        self.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        self.setAttribute(Qt.WidgetAttribute.WA_AlwaysStackOnTop, False)
        self.setClearColor(QColor(PALETTE["ELEVATE_1"]))
        self.setSource(str(_HEADER_QML))
        root_obj = self.rootObject()
        if root_obj is None:
            raise RuntimeError("Header.qml failed to load: rootObject is None")
        root_obj.setProperty("controller", controller)
