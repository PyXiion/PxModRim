from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt, Signal, Slot
from PySide6.QtGui import QColor
from PySide6.QtQml import QQmlEngine
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import QComboBox, QVBoxLayout, QWidget

from pxmodrim.core.models.view.sidebar import SidebarEntry
from pxmodrim.ui.models.sidebar_model import SidebarModel
from pxmodrim.ui.theme.palette import PALETTE

_QML_DIR = Path(__file__).parent
_SIDEBAR_QML = _QML_DIR / "Sidebar.qml"


class SidebarPanel(QWidget):
    entry_selected = Signal(object)  # SidebarEntry

    def __init__(self, qml_engine: QQmlEngine | None = None) -> None:
        super().__init__()
        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 12, 12, 12)
        layout.setSpacing(8)

        self.preset_combo = QComboBox()
        self.preset_combo.setObjectName("presetCombo")
        self.preset_combo.addItem("Default")
        layout.addWidget(self.preset_combo)

        # Model
        self._model = SidebarModel()

        # QML view
        self._qml = QQuickWidget(qml_engine, self)  # type: ignore[arg-type]
        self._qml.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        self._qml.setAttribute(Qt.WidgetAttribute.WA_AlwaysStackOnTop, False)
        self._qml.setClearColor(QColor(PALETTE["ELEVATE_2"]))

        ctx = self._qml.rootContext()
        ctx.setContextProperty("sidebarPanel", self)
        ctx.setContextProperty("sidebarModel", self._model)
        self._qml.setSource(str(_SIDEBAR_QML))

        layout.addWidget(self._qml)

        self._entries: list[SidebarEntry] = []

    def set_entries(self, entries: list[SidebarEntry]) -> None:
        self._entries = entries
        self._model.update_entries(entries)

    # ── Slots called from QML ─────────────────────────────────

    @Slot(int)
    def entrySelected(self, row: int) -> None:  # noqa: N802
        if 0 <= row < len(self._entries):
            self.entry_selected.emit(self._entries[row])
