from __future__ import annotations

from PySide6.QtCore import Property, QObject, Signal, Slot

from pxmodrim.core.constants import LaunchStrategy

_STRATEGY_LABELS = {
    LaunchStrategy.DIRECT: "Direct executable",
    LaunchStrategy.STEAM: "Steam",
}


class HeaderController(QObject):
    refresh_requested = Signal()
    sort_requested = Signal()
    save_requested = Signal()
    settings_requested = Signal()
    launch_requested = Signal()
    strategy_changed = Signal(int)
    minimize_requested = Signal()
    maximize_requested = Signal()
    close_requested = Signal()
    drag_started = Signal()
    unsaved_changes_changed = Signal()

    def __init__(
        self,
        is_frameless: bool = False,
        parent: QObject | None = None,
        initial_strategy: int = 0,
        app_version: str = "",
        tooltips: dict[str, str] | None = None,
    ) -> None:
        super().__init__(parent)
        self._is_frameless = is_frameless
        self._maximized = False
        self._strategy_index: int = initial_strategy
        self._unsaved_changes = False
        self._app_version = app_version
        self._tooltips = tooltips or {}

    def is_frameless_getter(self) -> bool:
        return self._is_frameless

    is_frameless = Property(bool, is_frameless_getter, constant=True)

    appVersion = Property(str, lambda self: self._app_version, constant=True)
    tooltips = Property(dict, lambda self: self._tooltips, constant=True)

    @Property("QVariantList", constant=True)  # type: ignore[operator]
    def strategies(self) -> list[dict[str, object]]:
        return [
            {"index": int(s), "label": _STRATEGY_LABELS[s]} for s in LaunchStrategy
        ]

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
    def dragStarted(self) -> None:
        self.drag_started.emit()

    # ── QML property ────────────────────────────────────────

    def _get_strategy_index(self) -> int:
        return self._strategy_index

    strategyIndex = Property(int, _get_strategy_index, notify=strategy_changed)

    def _get_unsaved_changes(self) -> bool:
        return self._unsaved_changes

    @Slot(bool)
    def set_unsaved_changes(self, value: bool) -> None:
        if self._unsaved_changes != value:
            self._unsaved_changes = value
            self.unsaved_changes_changed.emit()

    unsavedChanges = Property(
        bool,
        _get_unsaved_changes,
        set_unsaved_changes,
        notify=unsaved_changes_changed,
    )
