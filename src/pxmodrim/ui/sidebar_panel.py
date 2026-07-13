from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QObject, Qt, Signal, Slot
from PySide6.QtGui import QColor
from PySide6.QtQml import QQmlEngine
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import QComboBox, QVBoxLayout, QWidget

from pxmodrim.models.view.sidebar import SidebarEntry
from pxmodrim.ui.palette import PALETTE
from pxmodrim.ui.sidebar_model import SidebarModel

_QML_DIR = Path(__file__).parent / "qml"
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
        # Save current selection label before reset
        old_index = -1
        root = self._qml.rootObject()
        if root is not None:
            list_view = root.findChild(QObject, "listView")
            if list_view is not None:
                old_index = list_view.property("currentIndex")  # type: ignore[assignment]
        old_label = (
            self._entries[old_index].label
            if 0 <= old_index < len(self._entries)
            else None
        )

        self._entries = entries
        self._model.set_entries(entries)
        root = self._qml.rootObject()
        if root is not None and self._model.rowCount() > 0:
            list_view = root.findChild(QObject, "listView")
            if list_view is not None:
                # Restore selection by matching label
                restore_index = 0
                if old_label is not None:
                    for i, entry in enumerate(entries):
                        if entry.label == old_label:
                            restore_index = i
                            break
                list_view.setProperty("currentIndex", restore_index)

    def update_counts(self) -> None:
        self._model.update_counts(self._entries)

    # ── Slots called from QML ─────────────────────────────────

    @Slot(int)
    def entrySelected(self, row: int) -> None:  # noqa: N802
        if 0 <= row < len(self._entries):
            self.entry_selected.emit(self._entries[row])
