from __future__ import annotations

from collections.abc import Mapping

from PySide6.QtGui import QAction
from PySide6.QtWidgets import QMenuBar, QWidget

from pxmodrim.ui.window.actions import ActionId

MENU_LAYOUT: tuple[tuple[str, tuple[ActionId | None, ...]], ...] = (
    (
        "&File",
        (
            ActionId.SAVE,
            ActionId.RESTORE,
            ActionId.SETTINGS,
            None,
            ActionId.QUIT,
        ),
    ),
    (
        "&Mods",
        (ActionId.REFRESH, ActionId.FULL_RESCAN, ActionId.AUTO_SORT),
    ),
    (
        "&View",
        (
            ActionId.FOCUS_SEARCH,
            ActionId.NEXT_VIEW,
            ActionId.PREV_VIEW,
            None,
            ActionId.FULLSCREEN,
        ),
    ),
    (
        "&Help",
        (
            ActionId.REPORT_ISSUE,
            ActionId.UPLOAD_LOGS,
            ActionId.OPEN_LOGS,
            ActionId.SHORTCUTS,
            None,
            ActionId.ABOUT,
        ),
    ),
)


class MenuBar(QMenuBar):
    def __init__(
        self, actions: Mapping[ActionId, QAction], parent: QWidget | None = None
    ) -> None:
        super().__init__(parent)
        for title, action_ids in MENU_LAYOUT:
            menu = self.addMenu(title)
            for action_id in action_ids:
                if action_id is None:
                    menu.addSeparator()
                else:
                    menu.addAction(actions[action_id])
