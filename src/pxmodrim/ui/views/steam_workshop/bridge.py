from __future__ import annotations

from typing import TYPE_CHECKING

from loguru import logger
from PySide6.QtCore import QObject, QUrl, Signal, Slot
from PySide6.QtGui import QDesktopServices

if TYPE_CHECKING:
    from pxmodrim.ui.views.steam_workshop_view import SteamWorkshopViewPanel


class SteamWorkshopBridge(QObject):
    download_state_changed = Signal(str, str, bool)

    def __init__(
        self, panel: SteamWorkshopViewPanel, parent: QObject | None = None
    ) -> None:
        """Initialize the bridge with a reference to the parent workshop panel."""
        super().__init__(parent)
        self._panel = panel

    @Slot(str, result=bool)
    def has_mod(self, publishedfileid: str) -> bool:
        return publishedfileid in self._panel._installed_ids

    @Slot(str, str, bool)
    def toggle_download_checked(self, mod_id: str, title: str, checked: bool) -> None:
        if checked:
            self._panel._checked_ids[mod_id] = title
        else:
            title = self._panel._checked_ids.pop(mod_id, title)
        logger.debug(
            f"[steam] toggle_download_checked: {mod_id} checked={checked}"
            f" queue_size={len(self._panel._checked_ids)}"
        )
        self.download_state_changed.emit(mod_id, title, checked)

    @Slot(result="QStringList")
    def get_installed_ids(self) -> list[str]:
        return list(self._panel._installed_ids)

    @Slot(result="QStringList")
    def get_checked_ids(self) -> list[str]:
        return list(self._panel._checked_ids.keys())

    @Slot(str)
    def open_in_browser(self, url: str) -> None:
        logger.debug(f"[steam] open_in_browser: {url}")
        QDesktopServices.openUrl(QUrl(url))
