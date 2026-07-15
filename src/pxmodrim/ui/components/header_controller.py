from __future__ import annotations

from PySide6.QtCore import Property, QObject, Signal, Slot


class HeaderController(QObject):
    refresh_requested = Signal()
    sort_requested = Signal()
    save_requested = Signal()
    settings_requested = Signal()
    launch_requested = Signal()
    launch_menu_requested = Signal()
    strategy_changed = Signal(int)
    minimize_requested = Signal()
    maximize_requested = Signal()
    close_requested = Signal()
    drag_started = Signal()

    def __init__(
        self,
        is_frameless: bool = False,
        parent: QObject | None = None,
        initial_strategy: int = 0,
    ) -> None:
        super().__init__(parent)
        self._is_frameless = is_frameless
        self._maximized = False
        self._strategy_index: int = initial_strategy

    def is_frameless_getter(self) -> bool:
        return self._is_frameless

    is_frameless = Property(bool, is_frameless_getter, constant=True)

    def set_maximized(self, value: bool) -> None:
        if self._maximized != value:
            self._maximized = value
            self.maximized_changed.emit()

    maximized_changed = Signal()
    maximized = Property(
        bool, lambda self: self._maximized, set_maximized, notify=maximized_changed
    )

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

    @Slot()
    def launch(self) -> None:
        self.launch_requested.emit()

    @Slot()
    def minimize(self) -> None:
        self.minimize_requested.emit()

    @Slot()
    def maximize(self) -> None:
        self.maximize_requested.emit()

    @Slot()
    def closeWindow(self) -> None:
        self.close_requested.emit()

    @Slot(int)
    def setStrategy(self, index: int) -> None:
        if self._strategy_index != index:
            self._strategy_index = index
            self.strategy_changed.emit(index)

    @Slot()
    def showLaunchMenu(self) -> None:
        self.launch_menu_requested.emit()

    @Slot()
    def dragStarted(self) -> None:
        self.drag_started.emit()

    # ── QML property ────────────────────────────────────────

    def _get_strategy_index(self) -> int:
        return self._strategy_index

    strategyIndex = Property(int, _get_strategy_index, notify=strategy_changed)
