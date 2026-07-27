from __future__ import annotations

import asyncio
import contextlib
from typing import TYPE_CHECKING, NamedTuple, cast

import httpx
from loguru import logger
from PySide6.QtWidgets import QFileDialog, QMessageBox, QWidget

from pxmodrim.core.events import Event
from pxmodrim.core.loading import LoadingState
from pxmodrim.core.plugin import Plugin
from pxmodrim.core.services.steam_cmd_service import SymlinkConflictError
from pxmodrim.ui.components.dialogs import await_dialog
from pxmodrim.ui.components.progress_dialog import ProgressDialog

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext
    from pxmodrim.core.services.steam_cmd_service import (
        SteamCmdItemStatus,
        SteamCmdProgress,
        SteamCmdResult,
        SteamCmdService,
    )
    from pxmodrim.ui.context import AppContext



class SidebarSync(NamedTuple):
    checked_ids: dict[str, str]
    statuses: dict[str, str]


class ProgressInfo(NamedTuple):
    total: int
    completed: int
    downloading_id: str


class ItemStatus(NamedTuple):
    mod_id: str
    status: str


class SteamCmdUiPlugin(Plugin):
    name = "steamworkshop"
    dependencies = ["steamcmd"]

    badges_refresh_requested: Event[list[str]]
    sidebar_sync_requested: Event[SidebarSync]
    progress_updated: Event[ProgressInfo]
    item_status_changed: Event[ItemStatus]
    download_busy_changed: Event[bool]
    uncheck_mod_requested: Event[str]
    clear_checked_requested: Event[None]

    _svc: SteamCmdService

    def __init__(self) -> None:
        self.badges_refresh_requested = Event()
        self.sidebar_sync_requested = Event()
        self.progress_updated = Event()
        self.item_status_changed = Event()
        self.download_busy_changed = Event()
        self.uncheck_mod_requested = Event()
        self.clear_checked_requested = Event()

        self._core: CoreContext | None = None
        self._app_ctx: AppContext | None = None

        self._installed_ids: set[str] = set()
        self._checked_ids: dict[str, str] = {}
        self._download_statuses: dict[str, str] = {}
        self._current_downloading_id: str = ""

    # ── Plugin lifecycle ─────────────────────────────────

    def setup(self, ctx: AppContext) -> None:  # type: ignore[override]
        self._core = ctx.core
        self._app_ctx = ctx
        self._svc = cast("SteamCmdService", ctx.core.plugins.get("steamcmd"))
        from pxmodrim.ui.plugins.steam_workshop.view import SteamWorkshopViewPanel

        ctx.add_rail_view(SteamWorkshopViewPanel)

        ctx.core.mod_service.mods_changed.connect(self._on_mods_changed)
        self._svc.download_progress.connect(self._on_steam_progress)
        self._svc.download_item_status_changed.connect(self._on_steam_item_status)
        self._svc.download_finished.connect(self._on_steam_finished)

    async def init(self, ctx: AppContext) -> None:
        self._refresh_cached_ids()

    async def shutdown(self) -> None:
        if self._core is None:
            return
        with contextlib.suppress(ValueError):
            self._svc.download_progress.disconnect(self._on_steam_progress)
            self._svc.download_item_status_changed.disconnect(self._on_steam_item_status)
            self._svc.download_finished.disconnect(self._on_steam_finished)
            self._core.mod_service.mods_changed.disconnect(self._on_mods_changed)

    # ── JS action handlers (called by action_handler) ────

    def toggle_download_checked(self, mod_id: str, title: str, checked: bool) -> None:
        if checked:
            self._checked_ids[mod_id] = title
        else:
            self._checked_ids.pop(mod_id, None)
        self.sidebar_sync_requested.emit(
            SidebarSync(dict(self._checked_ids), dict(self._download_statuses))
        )

    def batch_toggle_download_checked(
        self, mod_ids: list[str], titles: list[str], checked: bool
    ) -> None:
        for mod_id, title in zip(mod_ids, titles, strict=True):
            if checked:
                self._checked_ids[mod_id] = title
            else:
                self._checked_ids.pop(mod_id, None)
        if mod_ids:
            self.sidebar_sync_requested.emit(
                SidebarSync(dict(self._checked_ids), dict(self._download_statuses))
            )

    # ── Sidebar callbacks (called by view) ───────────────

    async def request_download(self, parent_widget: QWidget, validate: bool) -> None:
        ids = list(self._checked_ids.keys())
        if not ids:
            logger.debug("[steam] download requested with empty queue")
            return
        logger.debug("[steam] download requested: %s", ids)

        self.download_busy_changed.emit(True)
        try:
            if not await self._ensure_steamcmd(parent_widget):
                return
            if not await self._ensure_symlink(parent_widget):
                return

            await self._svc.download_mods(
                ids,
                validate=validate,
                titles=dict(self._checked_ids),
            )
        finally:
            self.download_busy_changed.emit(False)

    def stop_download(self) -> None:
        logger.info("[steam] download stop requested")
        self._svc.cancel()
        self._current_downloading_id = ""
        self.progress_updated.emit(ProgressInfo(0, 0, ""))

    # ── UI helpers (moved from SteamCmdSetupPlugin) ──────

    async def _ensure_steamcmd(self, parent: QWidget) -> bool:
        if self._svc.is_installed():
            return True

        prefix = self._core.config.paths.steamcmd_prefix if self._core else ""
        if not prefix:
            folder = QFileDialog.getExistingDirectory(
                parent, "Select SteamCMD install folder"
            )
            if not folder:
                return False
            prefix = folder

        async with ProgressDialog(LoadingState(), parent) as dialog:
            loading = dialog.loading
            return await self._svc.ensure_installed(prefix, loading_state=loading)

    async def _ensure_symlink(self, parent: QWidget) -> bool:
        local = self._core.config.paths.local if self._core else ""
        if not local:
            self._svc.status_message_changed.emit("Local mods path is not configured.")
            return False

        try:
            await self._svc.ensure_symlink(local, forced=False)
            return True
        except OSError:
            await await_dialog(
                QMessageBox,
                QMessageBox.Icon.Critical,
                "Symlink Error",
                "Failed to create SteamCMD symlink. Check permissions or disk space.",
                QMessageBox.StandardButton.Ok,
                parent,
            )
            return False
        except SymlinkConflictError as exc:
            result, _ = await await_dialog(
                QMessageBox,
                QMessageBox.Icon.Warning,
                "Overwrite existing folder?",
                str(exc),
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                parent,
            )
            if result != QMessageBox.StandardButton.Yes:
                self._svc.status_message_changed.emit("Download cancelled by user.")
                return False

            await self._svc.ensure_symlink(local, forced=True)
            return True

    def remove_item(self, mod_id: str) -> None:
        logger.debug("[steam] download item removed: %s", mod_id)
        self._checked_ids.pop(mod_id, None)
        self._download_statuses.pop(mod_id, None)
        self.sidebar_sync_requested.emit(
            SidebarSync(dict(self._checked_ids), dict(self._download_statuses))
        )
        self.uncheck_mod_requested.emit(mod_id)

    def clear_queue(self) -> None:
        logger.info("[steam] download queue cleared (%d items)", len(self._checked_ids))
        self._checked_ids.clear()
        self._download_statuses.clear()
        self.sidebar_sync_requested.emit(SidebarSync({}, {}))
        self.clear_checked_requested.emit(None)

    def sync_all(self) -> None:
        self._refresh_cached_ids()
        self.badges_refresh_requested.emit(list(self._installed_ids))
        self.sidebar_sync_requested.emit(
            SidebarSync(dict(self._checked_ids), dict(self._download_statuses))
        )

    # ── Service event handlers ───────────────────────────

    def _on_mods_changed(self, _: None) -> None:
        self._refresh_cached_ids()
        self.badges_refresh_requested.emit(list(self._installed_ids))

    def _on_steam_progress(self, progress: SteamCmdProgress) -> None:
        self.progress_updated.emit(
            ProgressInfo(
                progress.total,
                progress.completed,
                self._current_downloading_id,
            )
        )

    def _on_steam_item_status(self, item: SteamCmdItemStatus) -> None:
        if item.status == "downloading":
            self._current_downloading_id = item.mod_id
        self._download_statuses[item.mod_id] = item.status
        self.item_status_changed.emit(ItemStatus(item.mod_id, item.status))

    def _on_steam_finished(self, result: SteamCmdResult) -> None:
        logger.info(
            "[steam] download finished: %d ok, %d failed",
            len(result.succeeded),
            len(result.failed),
        )
        for mid in result.succeeded:
            self._checked_ids.pop(mid, None)
            self._download_statuses.pop(mid, None)
        self._current_downloading_id = ""
        self.progress_updated.emit(ProgressInfo(0, 0, ""))
        self.sidebar_sync_requested.emit(
            SidebarSync(dict(self._checked_ids), dict(self._download_statuses))
        )
        if self._core is not None:
            asyncio.create_task(self._core.mod_service.discover())

    # ── Internal helpers ─────────────────────────────────

    def _refresh_cached_ids(self) -> None:
        if self._core is None:
            return
        self._installed_ids = {
            m.published_file_id
            for m in self._core.all_mods.values()
            if m.published_file_id
        }

    async def fetch_mod_deps(self, mod_id: str) -> str | None:
        url = f"https://deps.modrim.pyxiion.ru/deps?id={mod_id}"
        async with httpx.AsyncClient(timeout=30) as client:
            resp = await client.get(url, headers={"User-Agent": "PxModRim/1.0"})
            if resp.status_code != 200:
                logger.warning(
                    "[steam] fetch_mod_deps HTTP {} for {}",
                    resp.status_code,
                    mod_id,
                )
                return None
            return resp.text
