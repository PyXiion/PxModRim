from __future__ import annotations

import contextlib
from typing import TYPE_CHECKING, NamedTuple, cast

import httpx
from loguru import logger

from pxmodrim.core.events import Event
from pxmodrim.core.plugin import Plugin

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext
    from pxmodrim.core.downloads import DownloadItemStatus, DownloadResult
    from pxmodrim.core.downloads.steam import SteamDownloader
    from pxmodrim.ui.context import AppContext


class SidebarSync(NamedTuple):
    checked_ids: dict[str, str]
    statuses: dict[str, str]


class ItemStatus(NamedTuple):
    mod_id: str
    status: str
    bytes_done: int = 0
    bytes_total: int = 0


class SteamWorkshopUiPlugin(Plugin):
    name = "steamworkshop"
    dependencies = ["steam_downloader"]

    badges_refresh_requested: Event[list[str]]
    sidebar_sync_requested: Event[SidebarSync]
    item_status_changed: Event[ItemStatus]
    download_busy_changed: Event[bool]
    uncheck_mod_requested: Event[str]
    clear_checked_requested: Event[None]
    active_refresh_requested: Event[list[str]]

    _svc: SteamDownloader

    def __init__(self) -> None:
        self.badges_refresh_requested = Event()
        self.sidebar_sync_requested = Event()
        self.item_status_changed = Event()
        self.download_busy_changed = Event()
        self.uncheck_mod_requested = Event()
        self.clear_checked_requested = Event()
        self.active_refresh_requested = Event()

        self._core: CoreContext | None = None
        self._app_ctx: AppContext | None = None

        self._installed_ids: set[str] = set()
        self._active_ids: set[str] = set()
        self._checked_ids: dict[str, str] = {}
        self._download_statuses: dict[str, str] = {}
        # The service is shared with MainWindow's update actions; only batches
        # started from this view may drive the queue sidebar.
        self._own_download = False

    # ── Plugin lifecycle ─────────────────────────────────

    def setup(self, ctx: AppContext) -> None:  # type: ignore[override]
        self._core = ctx.core
        self._app_ctx = ctx
        self._svc = cast("SteamDownloader", ctx.core.plugins.get("steam_downloader"))
        from pxmodrim.ui.plugins.steam_workshop.view import SteamWorkshopViewPanel

        ctx.add_rail_view(SteamWorkshopViewPanel)

        ctx.core.mod_service.mods_changed.connect(self._on_mods_changed)
        ctx.core.active_state_changed.connect(self._on_active_state_changed)
        self._svc.download_item_status_changed.connect(self._on_item_status)
        self._svc.download_finished.connect(self._on_download_finished)
        self._svc.busy_changed.connect(self.download_busy_changed.emit)

    async def init(self, ctx: AppContext) -> None:
        self._refresh_cached_ids()

    async def shutdown(self) -> None:
        if self._core is None:
            return
        with contextlib.suppress(ValueError):
            self._svc.download_item_status_changed.disconnect(self._on_item_status)
            self._svc.download_finished.disconnect(self._on_download_finished)
            self._svc.busy_changed.disconnect(self.download_busy_changed.emit)
            self._core.mod_service.mods_changed.disconnect(self._on_mods_changed)
            self._core.active_state_changed.disconnect(
                self._on_active_state_changed
            )

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

    def toggle_active(self, mod_id: str, active: bool) -> None:
        if self._core is None:
            return
        uuid = next(
            (
                u
                for u, m in self._core.all_mods.items()
                if m.published_file_id == mod_id
            ),
            None,
        )
        if uuid is None:
            return
        current = set(self._core.active_uuids)
        if active:
            current.add(uuid)
        else:
            current.discard(uuid)
        self._core.set_active(list(current))

    def batch_toggle_active(self, mod_ids: list[str], active: bool) -> None:
        if self._core is None:
            return
        pub_to_uuid = {
            m.published_file_id: u
            for u, m in self._core.all_mods.items()
            if m.published_file_id
        }
        current = set(self._core.active_uuids)
        for mid in mod_ids:
            uuid = pub_to_uuid.get(mid)
            if uuid is None:
                continue
            if active:
                current.add(uuid)
            else:
                current.discard(uuid)
        self._core.set_active(list(current))

    # ── Sidebar callbacks (called by view) ───────────────

    async def request_download(self) -> None:
        ids = list(self._checked_ids.keys())
        if not ids:
            logger.debug("[steam] download requested with empty queue")
            return
        if self._core is None or not self._core.config.paths.local:
            self._svc.status_message_changed.emit("Local mods path is not configured")
            return
        if self._svc.is_downloading:
            self._svc.status_message_changed.emit("A download is already running")
            return
        logger.debug("[steam] download requested: {}", ids)

        self._own_download = True
        if self._app_ctx is not None:
            self._app_ctx.navigate("downloads")
        try:
            for mod_id in ids:
                self._download_statuses[mod_id] = "queued"
            self.sidebar_sync_requested.emit(
                SidebarSync(dict(self._checked_ids), dict(self._download_statuses))
            )
            await self._svc.download_mods(ids)
        finally:
            self._own_download = False

    def stop_download(self) -> None:
        logger.info("[steam] download stop requested")
        self._svc.cancel()

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
        self.active_refresh_requested.emit(list(self._active_ids))
        self.sidebar_sync_requested.emit(
            SidebarSync(dict(self._checked_ids), dict(self._download_statuses))
        )

    # ── Service event handlers ───────────────────────────

    def _on_active_state_changed(self, _: tuple[str, ...]) -> None:
        self._refresh_cached_ids()
        self.badges_refresh_requested.emit(list(self._installed_ids))
        self.active_refresh_requested.emit(list(self._active_ids))

    def _on_mods_changed(self, _: None) -> None:
        self._refresh_cached_ids()
        self.badges_refresh_requested.emit(list(self._installed_ids))
        self.active_refresh_requested.emit(list(self._active_ids))

    def _on_item_status(self, item: DownloadItemStatus) -> None:
        if not self._own_download:
            return
        self._download_statuses[item.mod_id] = item.status
        self.item_status_changed.emit(
            ItemStatus(item.mod_id, item.status, item.bytes_done, item.bytes_total)
        )

    def _on_download_finished(self, result: DownloadResult) -> None:
        if not self._own_download:
            return
        logger.info(
            "[steam] download finished: {} ok, {} failed",
            len(result.succeeded),
            len(result.failed),
        )
        for mid in result.succeeded:
            self._checked_ids.pop(mid, None)
            self._download_statuses.pop(mid, None)
        for mod_id, status in tuple(self._download_statuses.items()):
            if status in {"queued", "downloading"}:
                self._download_statuses.pop(mod_id)
        self.sidebar_sync_requested.emit(
            SidebarSync(dict(self._checked_ids), dict(self._download_statuses))
        )

    # ── Internal helpers ─────────────────────────────────

    def _refresh_cached_ids(self) -> None:
        if self._core is None:
            return
        self._installed_ids = {
            m.published_file_id
            for m in self._core.all_mods.values()
            if m.published_file_id
        }
        active_uuids = set(self._core.active_uuids)
        self._active_ids = {
            m.published_file_id
            for m in self._core.all_mods.values()
            if m.published_file_id and m.uuid in active_uuids
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
