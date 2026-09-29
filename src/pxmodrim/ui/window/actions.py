from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum

from PySide6.QtGui import QAction, QKeySequence
from PySide6.QtWidgets import QWidget


class ActionId(StrEnum):
    SAVE = "save"
    RESTORE = "restore"
    SETTINGS = "settings"
    QUIT = "quit"
    REFRESH = "refresh"
    FULL_RESCAN = "full_rescan"
    AUTO_SORT = "auto_sort"
    UPDATE_MODS = "update_mods"
    FOCUS_SEARCH = "focus_search"
    NEXT_VIEW = "next_view"
    PREV_VIEW = "prev_view"
    FULLSCREEN = "fullscreen"
    REPORT_ISSUE = "report_issue"
    UPLOAD_LOGS = "upload_logs"
    OPEN_LOGS = "open_logs"
    SHORTCUTS = "shortcuts"
    CHECK_UPDATES = "check_updates"
    ABOUT = "about"


@dataclass(frozen=True, slots=True)
class ActionSpec:
    id: ActionId
    menu_text: str
    label: str
    shortcut: str = ""

    def tooltip(self) -> str:
        if not self.shortcut:
            return self.label
        return f"{self.label} ({_native(self.shortcut)})"


ACTION_SPECS: tuple[ActionSpec, ...] = (
    ActionSpec(ActionId.SAVE, "&Save Mod List", "Save mod list", "Ctrl+S"),
    ActionSpec(ActionId.RESTORE, "&Restore Mod List\u2026", "Restore mod list"),
    ActionSpec(ActionId.SETTINGS, "&Settings\u2026", "Settings", "Ctrl+,"),
    ActionSpec(ActionId.QUIT, "&Quit", "Quit", "Ctrl+Q"),
    ActionSpec(ActionId.REFRESH, "&Refresh Mods", "Refresh mods", "F5"),
    ActionSpec(
        ActionId.FULL_RESCAN, "&Full Mod Rescan", "Full mod rescan", "Ctrl+Shift+R"
    ),
    ActionSpec(ActionId.AUTO_SORT, "&Auto-Sort Mods", "Auto-sort mods"),
    ActionSpec(ActionId.UPDATE_MODS, "&Update Mods", "Update downloaded mods"),
    ActionSpec(ActionId.FOCUS_SEARCH, "Focus &Search", "Focus search", "Ctrl+F"),
    ActionSpec(ActionId.NEXT_VIEW, "&Next View", "Next view", "Ctrl+Tab"),
    ActionSpec(ActionId.PREV_VIEW, "&Previous View", "Previous view", "Ctrl+Shift+Tab"),
    ActionSpec(ActionId.FULLSCREEN, "Toggle &Fullscreen", "Toggle fullscreen", "F11"),
    ActionSpec(ActionId.REPORT_ISSUE, "&Report Issue", "Report issue"),
    ActionSpec(
        ActionId.UPLOAD_LOGS,
        "Log && System &Info\u2026",
        "Log & system info",
    ),
    ActionSpec(ActionId.OPEN_LOGS, "Open &Logs Folder", "Open logs folder"),
    ActionSpec(ActionId.SHORTCUTS, "&Keyboard Shortcuts", "Keyboard shortcuts"),
    ActionSpec(ActionId.CHECK_UPDATES, "Check for &Updates\u2026", "Check for updates"),
    ActionSpec(ActionId.ABOUT, "&About PxModRim", "About PxModRim", "F1"),
)

ACTIONS: dict[ActionId, ActionSpec] = {spec.id: spec for spec in ACTION_SPECS}

VIEW_SWITCH_KEYS: tuple[str, ...] = tuple(f"Ctrl+{i}" for i in range(1, 10))

_WINDOW_ROWS: tuple[tuple[str, str], ...] = (
    ("Ctrl+1\u20139", "Switch to view"),
    ("Alt", "Show or hide menu bar"),
    ("Esc", "Hide menu bar"),
)


def _native(sequence: str) -> str:
    return QKeySequence(sequence).toString(QKeySequence.SequenceFormat.NativeText)


def create_actions(window: QWidget) -> dict[ActionId, QAction]:
    """Build every window action and register it on *window*.

    Adding the actions to the window (not just to the hidden menu bar) keeps
    their shortcuts live while the menu bar is hidden.
    """
    actions: dict[ActionId, QAction] = {}
    for spec in ACTION_SPECS:
        action = QAction(spec.menu_text, window)
        if spec.shortcut:
            action.setShortcut(QKeySequence(spec.shortcut))
        window.addAction(action)
        actions[spec.id] = action
    return actions


def shortcut_rows() -> tuple[tuple[str, str], ...]:
    rows = tuple((_native(s.shortcut), s.label) for s in ACTION_SPECS if s.shortcut)
    return rows + _WINDOW_ROWS
