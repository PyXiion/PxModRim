from __future__ import annotations

import asyncio
import contextlib
import functools
import re
import time
from collections.abc import Awaitable, Callable, Sequence
from os import PathLike
from pathlib import Path
from typing import TYPE_CHECKING, Protocol

import msgspec
import pxsteamdl
from loguru import logger

from pxmodrim.core.downloads.downloader import Downloader
from pxmodrim.core.downloads.types import (
    DownloadItemStatus,
    DownloadItemTitle,
    DownloadProgress,
    DownloadResult,
)

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext
    from pxmodrim.core.models.metadata.structures import ListedMod

# PxSteamDL runs one shared pool of parallel_items * threads_per_item workers.
MAX_PARALLEL_ITEMS = 8
MAX_THREADS_PER_ITEM = 16

_CANCELLED = "cancelled"
_PROGRESS_LOG_INTERVAL_S = 15.0
_PROGRESS_EMIT_INTERVAL_S = 0.1
_SYNC_FILE = "workshop_sync.json"
AUTO_STARTUP_DELAY_S = 45.0
AUTO_CHECK_INTERVAL_S = 600.0
_MAX_PUBLISHED_FILE_ID = 2**64 - 1
_PUBLISHED_FILE_ID_RE = re.compile(r"[0-9]+")


class WorkshopClient(Protocol):
    def download(
        self,
        ids: Sequence[int],
        root: str | PathLike[str],
        *,
        parallel_items: int = ...,
        threads_per_item: int = ...,
        on_progress: Callable[[pxsteamdl.Progress], None] | None = None,
        on_resolved: Callable[[pxsteamdl.ItemInfo], None] | None = None,
        cancel: pxsteamdl.CancelToken | None = None,
    ) -> list[pxsteamdl.Result]: ...


ClientFactory = Callable[[], Awaitable[WorkshopClient]]


async def _login() -> WorkshopClient:
    return await asyncio.to_thread(pxsteamdl.Client)


def _validate_published_file_ids(publishedfileids: list[str]) -> None:
    if any(
        not isinstance(pfid, str)
        or _PUBLISHED_FILE_ID_RE.fullmatch(pfid) is None
        or int(pfid) > _MAX_PUBLISHED_FILE_ID
        for pfid in publishedfileids
    ):
        raise ValueError(
            "Published file IDs must be ASCII decimal numbers within uint64."
        )


class WorkshopSyncState(msgspec.Struct):
    """When each Workshop item was last synced with Steam (unix seconds)."""

    synced: dict[str, float] = msgspec.field(default_factory=dict)


class _Batch:
    __slots__ = (
        "bytes",
        "failed",
        "last_emitted",
        "last_logged",
        "resolved",
        "seen",
        "started",
        "succeeded",
        "total",
    )

    def __init__(self, total: int) -> None:
        self.total = total
        self.started = time.monotonic()
        self.last_logged = self.started
        self.last_emitted = 0.0
        self.seen = 0
        self.resolved = 0
        self.bytes: dict[str, tuple[int, int]] = {}
        self.succeeded: list[str] = []
        self.failed: list[str] = []

    def changed(self) -> list[str]:
        return [pid for pid in self.succeeded if self.bytes.get(pid, (0, 0))[1] > 0]

    def progress(self) -> DownloadProgress:
        # pxsteamdl returns results only after the whole batch, so an item that
        # reports all its bytes (or 0/0 for an up-to-date copy) is counted as done
        # before its Result arrives.
        finished = set(self.succeeded) | set(self.failed)
        finished.update(
            pid for pid, (done, total) in self.bytes.items() if done >= total
        )
        return DownloadProgress(
            total=self.total,
            completed=len(finished),
            bytes_done=sum(done for done, _ in self.bytes.values()),
            bytes_total=sum(total for _, total in self.bytes.values()),
        )


class SteamDownloader(Downloader):
    """Downloads Steam Workshop items into the local mods folder via PxSteamDL."""

    name = "steam_downloader"
    label = "Steam Workshop"

    def __init__(self, client_factory: ClientFactory | None = None) -> None:
        super().__init__()
        self._ctx: CoreContext | None = None
        self._client_factory: ClientFactory = client_factory or _login
        self._client: WorkshopClient | None = None
        self._token: pxsteamdl.CancelToken | None = None
        self._active_ids: frozenset[str] = frozenset()
        self._sync = WorkshopSyncState()
        self._auto_task: asyncio.Task[None] | None = None
        self._running: asyncio.Future[list[pxsteamdl.Result]] | None = None

    def setup(self, ctx: CoreContext) -> None:
        super().setup(ctx)
        self._ctx = ctx

    async def init(self, ctx: CoreContext) -> None:
        self._sync = await asyncio.to_thread(
            ctx.config_service.load, _SYNC_FILE, WorkshopSyncState
        )
        self._auto_task = asyncio.create_task(self._auto_update_loop())

    async def shutdown(self) -> None:
        if self._auto_task is not None:
            self._auto_task.cancel()
            await asyncio.wait([self._auto_task])
            self._auto_task = None
        self.cancel()
        running = self._running
        if running is not None:
            await asyncio.wait([running])

    @property
    def active_ids(self) -> frozenset[str]:
        """Published file ids of the batch currently downloading."""
        return self._active_ids

    @property
    def is_downloading(self) -> bool:
        return self._token is not None

    def accepts(self, mod_id: str) -> bool:
        return _PUBLISHED_FILE_ID_RE.fullmatch(mod_id) is not None

    async def download_mods(self, mod_ids: list[str]) -> DownloadResult:
        """Download items into ``paths.local/<id>/``; per-item failures don't raise.

        ``cancel()`` stops the batch: finished items stay, the rest are dropped.
        """
        if not mod_ids:
            raise ValueError("No mods selected for download.")
        _validate_published_file_ids(mod_ids)
        if self._ctx is None:
            raise RuntimeError("SteamDownloader used before setup()")
        local = self._ctx.config.paths.local
        if not local:
            raise ValueError("Local mods path is not configured.")
        if self.is_downloading:
            raise RuntimeError("A download is already running.")

        ids = list(dict.fromkeys(mod_ids))
        root = Path(local)
        batch = _Batch(len(ids))
        token = pxsteamdl.CancelToken()
        self._token = token
        self._active_ids = frozenset(ids)
        self.busy_changed.emit(True)
        self.batch_started.emit(list(ids))
        logger.info(
            "[workshop] downloading {} items into {} (first ids: {})",
            len(ids),
            root,
            ids[:5],
        )
        try:
            await self._run(ids, root, batch, token)
        finally:
            self._token = None
            self._active_ids = frozenset()
            self.busy_changed.emit(False)
            result = DownloadResult(
                succeeded=batch.succeeded,
                failed=batch.failed,
                changed=batch.changed(),
            )
            logger.info(
                "[workshop] finished in {:.1f}s: {} ok, {} failed{}",
                time.monotonic() - batch.started,
                len(result.succeeded),
                len(result.failed),
                f" (failed ids: {result.failed[:20]})" if result.failed else "",
            )
            self._record_synced(result.succeeded)
            self.download_finished.emit(result)
        return result

    def updatable_id(self, mod: ListedMod) -> str | None:
        """Workshop id of *mod* if it is a PxModRim download in ``paths.local``."""
        if self._ctx is None or not self._ctx.config.paths.local:
            return None
        path = mod.mod_path
        if path is None or path.parent != Path(self._ctx.config.paths.local):
            return None
        pfid = mod.published_file_id
        return pfid if pfid == path.name else None

    def last_synced(self, mod: ListedMod) -> float | None:
        pid = self.updatable_id(mod)
        return self._sync.synced.get(pid) if pid else None

    def stale_ids(self, max_age_s: float, now: float | None = None) -> list[str]:
        """Updatable ids never synced or synced over *max_age_s* ago, oldest first."""
        cutoff = (time.time() if now is None else now) - max_age_s
        synced = self._sync.synced
        if self._ctx is None:
            return []
        stale = [
            pid
            for pid in self.updatable_ids(self._ctx.all_mods.values())
            if synced.get(pid, 0.0) < cutoff
        ]
        return sorted(stale, key=lambda pid: synced.get(pid, 0.0))

    def _record_synced(self, ids: list[str]) -> None:
        if not ids or self._ctx is None:
            return
        now = time.time()
        self._sync.synced.update(dict.fromkeys(ids, now))
        try:
            self._ctx.config_service.save(_SYNC_FILE, self._sync)
        except OSError as exc:
            logger.warning("[workshop] cannot save sync times: {}", exc)

    async def _auto_update_loop(self) -> None:
        await asyncio.sleep(AUTO_STARTUP_DELAY_S)
        while True:
            try:
                await self._auto_update_once()
            except Exception:  # noqa: BLE001
                logger.exception("[workshop] auto-update iteration failed")
            await asyncio.sleep(AUTO_CHECK_INTERVAL_S)

    async def _auto_update_once(self) -> None:
        if self._ctx is None or self.is_downloading:
            return
        hours = self._ctx.config.workshop_auto_update_hours
        if hours <= 0:
            return
        ids = self.stale_ids(hours * 3600)
        if not ids:
            return
        logger.info("[workshop] auto-update: {} mods older than {}h", len(ids), hours)
        try:
            await self.download_mods(ids)
        except (RuntimeError, ValueError) as exc:
            logger.warning("[workshop] auto-update not started: {}", exc)

    def cancel(self) -> None:
        token = self._token
        if token is not None:
            logger.debug("[workshop] cancel requested")
            token.cancel()

    async def _run(
        self,
        ids: list[str],
        root: Path,
        batch: _Batch,
        token: pxsteamdl.CancelToken,
    ) -> None:
        try:
            await asyncio.to_thread(root.mkdir, parents=True, exist_ok=True)
            login_started = time.monotonic()
            if self._client is None:
                self.download_phase_changed.emit("login")
            client = await self._ensure_client()
            logger.debug(
                "[workshop] steam session ready in {:.1f}s",
                time.monotonic() - login_started,
            )
        except (RuntimeError, OSError) as exc:
            message = (
                f"Steam login failed: {exc}"
                if isinstance(exc, RuntimeError)
                else f"Cannot create {root}: {exc}"
            )
            logger.error("[workshop] {}", message)
            self.status_message_changed.emit(message)
            for pid in ids:
                self._finish_item(batch, pid, error=message)
            return

        cfg = self._ctx.config if self._ctx is not None else None
        parallel_items = min(
            MAX_PARALLEL_ITEMS, max(1, cfg.workshop_parallel_items if cfg else 8)
        )
        threads_per_item = min(
            MAX_THREADS_PER_ITEM, max(1, cfg.workshop_threads_per_item if cfg else 1)
        )
        logger.debug(
            "[workshop] client.download start: {} items, {} parallel x {} threads",
            len(ids),
            parallel_items,
            threads_per_item,
        )
        loop = asyncio.get_running_loop()

        def on_resolved(info: pxsteamdl.ItemInfo) -> None:
            # Called on the download() thread.
            loop.call_soon_threadsafe(
                self._on_resolved, batch, str(info.item_id), info.title
            )

        def on_progress(p: pxsteamdl.Progress) -> None:
            loop.call_soon_threadsafe(
                self._on_progress,
                batch,
                str(p.item_id),
                p.unpacked_bytes,
                p.unpacked_total,
            )

        self.download_phase_changed.emit("query")
        running = loop.run_in_executor(
            None,
            functools.partial(
                client.download,
                [int(pid) for pid in ids],
                root,
                parallel_items=parallel_items,
                threads_per_item=threads_per_item,
                on_progress=on_progress,
                on_resolved=on_resolved,
                cancel=token,
            ),
        )
        self._running = running
        try:
            results = await asyncio.shield(running)
        except asyncio.CancelledError:
            token.cancel()
            while not running.done():
                with contextlib.suppress(asyncio.CancelledError):
                    await asyncio.wait([running])
            raise
        except (RuntimeError, OSError) as exc:
            self._client = None
            for pid in ids:
                self._finish_item(batch, pid, error=str(exc) or type(exc).__name__)
            return
        finally:
            self._running = None

        # Flush progress callbacks queued before the executor returned.
        await asyncio.sleep(0)
        by_id = {str(r.item_id): r for r in results}
        for pid in ids:
            result = by_id.get(pid)
            if result is None:
                self._finish_item(batch, pid, error="no result")
            elif not result.cancelled:
                self._finish_item(batch, pid, error=result.error)

    async def _ensure_client(self) -> WorkshopClient:
        if self._client is None:
            self.status_message_changed.emit("Logging in to Steam\u2026")
            self._client = await self._client_factory()
        return self._client

    def _on_resolved(self, batch: _Batch, pid: str, title: str) -> None:
        batch.resolved += 1
        if title:
            self.download_item_titled.emit(DownloadItemTitle(pid, title))
        if batch.resolved >= batch.total:
            self.download_phase_changed.emit("run")

    def _on_progress(self, batch: _Batch, pid: str, done: int, total: int) -> None:
        if pid in batch.succeeded or pid in batch.failed:
            return
        if pid not in batch.bytes:
            batch.seen += 1
            logger.debug(
                "[workshop] {} first progress: {}/{} bytes (item {} of {})",
                pid,
                done,
                total,
                batch.seen,
                batch.total,
            )
        batch.bytes[pid] = (done, total)
        now = time.monotonic()
        # pxsteamdl reports per network chunk (thousands/s); UI needs far less.
        if done >= total or now - batch.last_emitted >= _PROGRESS_EMIT_INTERVAL_S:
            batch.last_emitted = now
            self._emit_item_progress(batch, pid, done, total)
        if now - batch.last_logged >= _PROGRESS_LOG_INTERVAL_S:
            batch.last_logged = now
            progress = batch.progress()
            logger.info(
                "[workshop] progress: {}/{} items, {:.1f} MB, {:.0f}s elapsed",
                progress.completed,
                progress.total,
                progress.bytes_done / 2**20,
                now - batch.started,
            )

    def _emit_item_progress(
        self, batch: _Batch, pid: str, done: int, total: int
    ) -> None:
        self.download_item_status_changed.emit(
            DownloadItemStatus(
                mod_id=pid, status="downloading", bytes_done=done, bytes_total=total
            )
        )
        self.download_progress.emit(batch.progress())

    def _finish_item(self, batch: _Batch, pid: str, *, error: str) -> None:
        _, total = batch.bytes.get(pid, (0, 0))
        if error:
            logger.warning("[workshop] {} failed: {}", pid, error)
            batch.failed.append(pid)
            status = DownloadItemStatus(mod_id=pid, status="error", error=error)
        else:
            batch.succeeded.append(pid)
            batch.bytes[pid] = (total, total)
            status = DownloadItemStatus(
                mod_id=pid, status="success", bytes_done=total, bytes_total=total
            )
        self.download_item_status_changed.emit(status)
        self.download_progress.emit(batch.progress())
