from __future__ import annotations

import asyncio
from typing import TYPE_CHECKING

import httpx
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

    @Slot(list, list, bool)
    def batch_toggle_download_checked(
        self, mod_ids: list[str], titles: list[str], checked: bool
    ) -> None:
        for mod_id, title in zip(mod_ids, titles, strict=True):
            if checked:
                self._panel._checked_ids[mod_id] = title
            else:
                self._panel._checked_ids.pop(mod_id, None)
            self.download_state_changed.emit(mod_id, title, checked)
        if mod_ids:
            logger.debug(
                f"[steam] batch_toggle_download_checked: {len(mod_ids)} mods"
                f" checked={checked} queue_size={len(self._panel._checked_ids)}"
            )

    @Slot(result="QStringList")
    def get_installed_ids(self) -> list[str]:
        return list(self._panel._installed_ids)

    @Slot(result="QStringList")
    def get_checked_ids(self) -> list[str]:
        return list(self._panel._checked_ids.keys())

    @Slot(str)
    def fetch_mod_deps(self, mod_id: str) -> None:
        """Kick off an async fetch of the dependency tree for *mod_id*.

        Result is delivered via ``_on_deps_fetched`` on the panel, which
        calls ``window.__pxmDepsFetched`` in the web view.
        """
        asyncio.create_task(self._do_fetch_deps(mod_id))

    async def _do_fetch_deps(self, mod_id: str) -> None:
        try:
            result = await asyncio.wait_for(
                asyncio.to_thread(self._sync_fetch_deps, mod_id),
                timeout=15.0,
            )
            json_result = result if result else "null"
        except Exception as e:
            logger.warning("[steam] fetch_mod_deps failed for {}: {}", mod_id, e)
            json_result = "null"
        self._panel._on_deps_fetched(mod_id, json_result)

    def _sync_fetch_deps(self, mod_id: str) -> str | None:
        url = f"https://deps.modrim.pyxiion.ru/deps?id={mod_id}"
        with httpx.Client(timeout=10) as client:
            resp = client.get(url, headers={"User-Agent": "PxModRim/1.0"})
            if resp.status_code != 200:
                logger.warning(
                    "[steam] fetch_mod_deps HTTP {} for {}", resp.status_code, mod_id
                )
                return None
            return resp.text

    @Slot(str)
    def open_in_browser(self, url: str) -> None:
        logger.debug(f"[steam] open_in_browser: {url}")
        QDesktopServices.openUrl(QUrl(url))
