from __future__ import annotations

from PySide6.QtCore import QObject, Signal, Slot


class HeaderController(QObject):
    refresh_requested = Signal()
    sort_requested = Signal()
    save_requested = Signal()
    settings_requested = Signal()

    @Slot()
    def refresh(self) -> None:
        self.refresh_requested.emit()

    @Slot()
    def autoSort(self) -> None:
        self.sort_requested.emit()

    @Slot()
    def save(self) -> None:
        self.save_requested.emit()

    @Slot()
    def openSettings(self) -> None:
        self.settings_requested.emit()
