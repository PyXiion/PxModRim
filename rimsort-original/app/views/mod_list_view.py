from __future__ import annotations

from typing import Any

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import QAbstractItemView, QListView


class ModListView(QListView):
    """QListView subclass configured for RimSort mod lists with drag-drop, selection, and context menu support."""

    key_press_signal = Signal(str)
    list_update_signal = Signal(str)

    def __init__(self, list_type: str, parent: Any = None) -> None:
        super().__init__(parent)
        self.list_type = list_type

        self.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
        self.setDragEnabled(True)
        self.setAcceptDrops(True)
        self.setDragDropMode(QAbstractItemView.DragDropMode.DragDrop)
        self.setDefaultDropAction(Qt.DropAction.MoveAction)
        self.setDropIndicatorShown(True)

        self.setUniformItemSizes(True)

        self.horizontalScrollBar().setEnabled(False)
        self.horizontalScrollBar().setVisible(False)

        self.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.customContextMenuRequested.connect(self._on_context_menu)

    def _on_context_menu(self, pos: Any) -> None:
        """Override in ModsPanel to show the mod-specific context menu."""
        index = self.indexAt(pos)
        if not index.isValid():
            return
        proxy = self.model()
        if proxy and hasattr(proxy, "mapToSource"):
            source_index = proxy.mapToSource(index)
            uuid = source_index.data(Qt.ItemDataRole.UserRole + 1)
            if uuid:
                self.context_menu_requested.emit(uuid, self.mapToGlobal(pos))

    context_menu_requested = Signal(str, object)
