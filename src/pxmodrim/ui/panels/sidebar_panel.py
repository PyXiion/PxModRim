from __future__ import annotations

from PySide6.QtCore import Signal
from PySide6.QtQml import QQmlEngine
from PySide6.QtWidgets import QWidget

from pxmodrim.core.context import CoreContext
from pxmodrim.core.models.view.sidebar import SidebarEntry
from pxmodrim.ui.components.filter_sidebar import FilterSidebar
from pxmodrim.ui.models.sidebar_model import SidebarModel


class SidebarPanel(FilterSidebar):
    entry_selected = Signal(object)  # SidebarEntry

    def __init__(
        self,
        ctx: CoreContext,
        qml_engine: QQmlEngine | None = None,
        parent: QWidget | None = None,
    ) -> None:
        self._model = SidebarModel()
        super().__init__(qml_engine, self._model, parent)
        self._model.setParent(self)
        self._entries: list[SidebarEntry] = []
        self.activated.connect(self._on_activated)
        ctx.diagnostics_service.sidebar_entries_changed.connect(self.set_entries)

    def set_entries(self, entries: list[SidebarEntry]) -> None:
        reset = len(entries) != len(self._entries)
        self._entries = entries
        self._model.update_entries(entries)
        if reset:
            current = self.current_entry()
            if current is not None:
                self.entry_selected.emit(current)

    def current_entry(self) -> SidebarEntry | None:
        selected = self.current_index()
        if 0 <= selected < len(self._entries):
            return self._entries[selected]
        return None

    def _on_activated(self, row: int) -> None:
        if 0 <= row < len(self._entries):
            self.entry_selected.emit(self._entries[row])
