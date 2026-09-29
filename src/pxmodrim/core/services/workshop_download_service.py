from __future__ import annotations

import asyncio
import contextlib
import functools
import re
import time
from collections.abc import Awaitable, Callable, Iterable, Sequence
from os import PathLike
from pathlib import Path
from typing import TYPE_CHECKING, Protocol

import msgspec
import pxsteamdl
from loguru import logger

from pxmodrim.core.events import Event
from pxmodrim.core.plugin import Plugin

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext
    from pxmodrim.core.models.metadata.structures import ListedMod

# PxSteamDL runs one shared pool of parallel_items * threads_per_item workers.
PARALLEL_ITEMS = 2
THREADS_PER_ITEM = 4

_CANCELLED = "cancelled"
_PROGRESS_LOG_INTERVAL_S = 15.0
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


class DownloadProgress(msgspec.Struct):
    total: int
    completed: int
    bytes_done: int
    bytes_total: int


class DownloadItemStatus(msgspec.Struct):
    mod_id: str
    status: str  # "downloading" | "success" | "error"
    bytes_done: int = 0
    bytes_total: int = 0
    error: str = ""


class DownloadResult(msgspec.Struct):
    succeeded: list[str]
    failed: list[str]


class _Batch:
    __slots__ = (
        "bytes",
        "failed",
        "last_logged",
        "seen",
        "started",
        "succeeded",
        "total",
    )

    def __init__(self, total: int) -> None:
        self.total = total
        self.started = time.monotonic()
        self.last_logged = self.started
        self.seen = 0
        self.bytes: dict[str, tuple[int, int]] = {}
        self.succeeded: list[str] = []
        self.failed: list[str] = []

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


class WorkshopDownloadService(Plugin):
    """Downloads Steam Workshop items into the local mods folder via PxSteamDL."""

    name = "workshop_download"

    status_message_changed: Event[str]
    download_progress: Event[DownloadProgress]
    download_item_status_changed: Event[DownloadItemStatus]
    download_finished: Event[DownloadResult]
    busy_changed: Event[bool]

    __slots__ = (
        "_client",
        "_client_factory",
        "_ctx",
        "_running",
        "_token",
        "busy_changed",
        "download_finished",
        "download_item_status_changed",
        "download_progress",
        "status_message_changed",
    )

    def __init__(self, client_factory: ClientFactory | None = None) -> None:
        self.status_message_changed = Event()
        self.download_progress = Event()
        self.download_item_status_changed = Event()
        self.download_finished = Event()
        self.busy_changed = Event()

        self._ctx: CoreContext | None = None
        self._client_factory: ClientFactory = client_factory or _login
        self._client: WorkshopClient | None = None
        self._token: pxsteamdl.CancelToken | None = None
        self._running: asyncio.Future[list[pxsteamdl.Result]] | None = None

    def setup(self, ctx: CoreContext) -> None:
        self._ctx = ctx

    async def init(self, ctx: CoreContext) -> None: ...

    async def shutdown(self) -> None:
        self.cancel()
        running = self._running
        if running is not None:
            await asyncio.wait([running])

    @property
    def is_downloading(self) -> bool:
        return self._token is not None

    async def download_mods(self, publishedfileids: list[str]) -> DownloadResult:
        """Download items into ``paths.local/<id>/``; per-item failures don't raise.

        ``cancel()`` stops the batch: finished items stay, the rest are dropped.
        """
        if not publishedfileids:
            raise ValueError("No mods selected for download.")
        _validate_published_file_ids(publishedfileids)
        if self._ctx is None:
            raise RuntimeError("WorkshopDownloadService used before setup()")
        local = self._ctx.config.paths.local
        if not local:
            raise ValueError("Local mods path is not configured.")
        if self.is_downloading:
            raise RuntimeError("A download is already running.")

        ids = list(dict.fromkeys(publishedfileids))
        root = Path(local)
        batch = _Batch(len(ids))
        token = pxsteamdl.CancelToken()
        self._token = token
        self.busy_changed.emit(True)
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
            self.busy_changed.emit(False)
            result = DownloadResult(succeeded=batch.succeeded, failed=batch.failed)
            logger.info(
                "[workshop] finished in {:.1f}s: {} ok, {} failed{}",
                time.monotonic() - batch.started,
                len(result.succeeded),
                len(result.failed),
                f" (failed ids: {result.failed[:20]})" if result.failed else "",
            )
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

    def updatable_ids(self, mods: Iterable[ListedMod] | None = None) -> list[str]:
        """Updatable ids among *mods* (all loaded mods by default), deduplicated."""
        if self._ctx is None:
            return []
        source = self._ctx.all_mods.values() if mods is None else mods
        ids = (self.updatable_id(m) for m in source)
        return list(dict.fromkeys(pid for pid in ids if pid is not None))

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

        logger.debug(
            "[workshop] client.download start: {} items, {} parallel x {} threads",
            len(ids),
            PARALLEL_ITEMS,
            THREADS_PER_ITEM,
        )
        loop = asyncio.get_running_loop()

        def on_progress(p: pxsteamdl.Progress) -> None:
            loop.call_soon_threadsafe(
                self._on_progress, batch, str(p.item_id), p.bytes_done, p.bytes_total
            )

        running = loop.run_in_executor(
            None,
            functools.partial(
                client.download,
                [int(pid) for pid in ids],
                root,
                parallel_items=PARALLEL_ITEMS,
                threads_per_item=THREADS_PER_ITEM,
                on_progress=on_progress,
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
            for pid in ids:
                self._finish_item(batch, pid, error=str(exc) or type(exc).__name__)
            return
        finally:
            self._running = None

        # Flush progress callbacks queued before the executor returned.
        await asyncio.sleep(0)
        by_id = {str(r.item_id): r.error for r in results}
        for pid in ids:
            error = by_id.get(pid, "no result")
            if error != _CANCELLED:
                self._finish_item(batch, pid, error=error)

    async def _ensure_client(self) -> WorkshopClient:
        if self._client is None:
            self.status_message_changed.emit("Logging in to Steam\u2026")
            self._client = await self._client_factory()
        return self._client

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


def workshop_service(ctx: CoreContext) -> WorkshopDownloadService | None:
    svc = ctx.plugins.get(WorkshopDownloadService.name)
    return svc if isinstance(svc, WorkshopDownloadService) else None
