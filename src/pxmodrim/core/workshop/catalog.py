from __future__ import annotations

import asyncio
import hashlib
import time
from collections.abc import AsyncIterator, Awaitable, Callable
from typing import TYPE_CHECKING, Any, ClassVar, Generic, TypeVar

import msgspec
from loguru import logger

from pxmodrim.core.downloads.manager import DownloadManager, download_manager
from pxmodrim.core.events import Event
from pxmodrim.core.plugin import Plugin
from pxmodrim.core.plugin_config import PluginConfig
from pxmodrim.core.workshop.client import CatalogClient, CatalogError
from pxmodrim.core.workshop.favourites import FavouriteCollections
from pxmodrim.core.workshop.types import (
    CatalogCollection,
    CatalogMod,
    CatalogPage,
    CatalogQuery,
    CollectionDetail,
    Discover,
    DownloadPlan,
    InstallState,
)

if TYPE_CHECKING:
    from pathlib import Path

    from pxmodrim.core.config import AppConfig
    from pxmodrim.core.context import CoreContext
    from pxmodrim.core.downloads.types import DownloadResult
    from pxmodrim.core.models.metadata.structures import ListedMod

FRESH_SECONDS = 300.0
_CACHE_LIMIT = 200
_DISK_LIMIT = 300

_V = TypeVar("_V")


class _Stored(msgspec.Struct, Generic[_V]):  # noqa: UP046
    at: float
    value: _V


def _cache_file(directory: Path, key: str) -> Path:
    return directory / f"{hashlib.sha256(key.encode()).hexdigest()[:32]}.json"


def _read_stored(path: Path, value_type: Any) -> tuple[float, Any] | None:
    stored_type: Any = _Stored[value_type]
    try:
        stored = msgspec.json.decode(path.read_bytes(), type=stored_type)
    except (OSError, msgspec.DecodeError):
        return None
    return stored.at, stored.value


def _write_stored(directory: Path, path: Path, entry: tuple[float, Any]) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_bytes(msgspec.json.encode(_Stored(entry[0], entry[1])))
    temporary.replace(path)
    files = sorted(directory.glob("*.json"), key=lambda file: file.stat().st_mtime)
    for stale in files[:-_DISK_LIMIT]:
        stale.unlink(missing_ok=True)


DEFAULT_CATALOG_URL = "https://api.modrim.pyxiion.dev"


class CatalogSettings(msgspec.Struct, frozen=True):
    url: str = DEFAULT_CATALOG_URL


class WorkshopCatalog(Plugin):
    name = "workshop_catalog"
    dependencies: ClassVar[list[str]] = ["downloads"]

    def __init__(
        self,
        client_factory: Callable[[str], CatalogClient] = CatalogClient,
    ) -> None:
        self.installed_changed: Event[None] = Event()
        self.queue_changed: Event[None] = Event()
        self._queue: list[list[str]] = []
        self._queued: set[str] = set()
        self._runner: asyncio.Future[None] | None = None
        self._idle = asyncio.Event()
        self.catalog_url_changed: Event[str] = Event()
        self.settings = PluginConfig(None, self.name, CatalogSettings)
        self.settings.changed.connect(self._on_settings_changed)
        self.favourites = FavouriteCollections(None)
        self._client_factory = client_factory
        self._base_url = self.settings.value.url.strip().rstrip("/")
        self._client: CatalogClient | None = None
        self._clients: list[CatalogClient] = []
        self._ctx: CoreContext | None = None
        self._manager: DownloadManager | None = None
        self._installed_index: dict[str, list[ListedMod]] | None = None
        self._responses: dict[str, tuple[float, Any]] = {}
        self._cache_dir: Path | None = None

    def setup(self, ctx: CoreContext) -> None:
        self._ctx = ctx
        if ctx.has_config_service:
            self.settings = PluginConfig(
                ctx.config_service,
                self.name,
                CatalogSettings,
                legacy={"workshop_catalog_url": "url"},
            )
            self.settings.changed.connect(self._on_settings_changed)
            self._change_url(self.settings.value.url)
            self._cache_dir = ctx.config_service.config_dir / "workshop-cache"
            self.favourites = FavouriteCollections(ctx.config_service)
        self._manager = download_manager(ctx)
        ctx.mod_service.mods_changed.connect(self._on_installed_changed)
        self._manager.download_finished.connect(self._on_download_finished)
        self._manager.busy_changed.connect(self._on_busy_changed)
        ctx.config_changed.connect(self._on_config_changed)

    def _on_installed_changed(self, _: None) -> None:
        self._installed_index = None
        self.installed_changed.emit(None)

    def _on_download_finished(self, _: DownloadResult) -> None:
        self._installed_index = None
        self.installed_changed.emit(None)

    def _on_config_changed(self, _config: AppConfig) -> None:
        self._installed_index = None
        self.installed_changed.emit(None)

    def _on_settings_changed(self, settings: CatalogSettings) -> None:
        self._change_url(settings.url)

    def _change_url(self, url: str) -> None:
        url = url.strip().rstrip("/")
        if self._base_url == url:
            return
        self._base_url = url
        self._client = None
        self.catalog_url_changed.emit(url)

    def set_base_url(self, url: str) -> None:
        self.settings.update(CatalogSettings(url.strip().rstrip("/")))

    @property
    def configured(self) -> bool:
        return bool(self._base_url)

    def _catalog_client(self) -> CatalogClient:
        if not self.configured:
            raise CatalogError("Workshop catalog is disabled.")
        if self._client is None:
            self._client = self._client_factory(self._base_url)
            # Keep replaced clients alive until shutdown so in-flight requests finish.
            self._clients.append(self._client)
        return self._client

    async def shutdown(self) -> None:
        self.cancel_queue()
        if self._runner is not None:
            self._runner.cancel()
            self._runner = None
        if self._ctx is not None:
            self._ctx.mod_service.mods_changed.disconnect(self._on_installed_changed)
            self._ctx.config_changed.disconnect(self._on_config_changed)
        if self._manager is not None:
            self._manager.download_finished.disconnect(self._on_download_finished)
            self._manager.busy_changed.disconnect(self._on_busy_changed)
        for client in self._clients:
            await client.shutdown()
        self._clients.clear()
        self._client = None

    async def discover(self) -> Discover:
        return await self._catalog_client().discover()

    async def mods(self, q: CatalogQuery) -> CatalogPage[CatalogMod]:
        return await self._catalog_client().mods(q)

    async def collections(self, q: CatalogQuery) -> CatalogPage[CatalogCollection]:
        return await self._catalog_client().collections(q)

    async def mod(self, id: str) -> CatalogMod | None:
        return await self._catalog_client().mod(id)

    async def collection(self, id: str) -> CollectionDetail | None:
        return await self._catalog_client().collection(id)

    def _installed(self) -> dict[str, list[ListedMod]]:
        if self._ctx is None:
            raise RuntimeError("Workshop catalog accessed before setup")
        if self._installed_index is None:
            installed: dict[str, list[ListedMod]] = {}
            for mod in self._ctx.all_mods.values():
                id = mod.published_file_id
                if id is not None:
                    installed.setdefault(id, []).append(mod)
            self._installed_index = installed
        return self._installed_index

    async def cached[T](
        self,
        key: str,
        produce: Callable[[], Awaitable[T]],
        fresh_for: float = FRESH_SECONDS,
        value_type: Any = None,
    ) -> AsyncIterator[T]:
        """Yield the remembered value at once, then the refreshed one if it differs.

        A value younger than *fresh_for* is served without contacting the catalog.
        With *value_type*, values are also kept on disk, so they survive a restart.
        """
        key = f"{self._base_url}|{key}"
        entry = self._responses.get(key)
        directory = self._cache_dir if value_type is not None else None
        if entry is None and directory is not None:
            entry = await asyncio.to_thread(
                _read_stored, _cache_file(directory, key), value_type
            )
        if entry is not None:
            self._responses[key] = entry
            while len(self._responses) > _CACHE_LIMIT:
                del self._responses[next(iter(self._responses))]
            yield entry[1]
            if time.time() - entry[0] < fresh_for:
                return
        value = await produce()
        changed = entry is None or value != entry[1]
        if entry is not None and not changed:
            value = entry[1]
        remembered = (time.time(), value)
        self._responses.pop(key, None)
        self._responses[key] = remembered
        while len(self._responses) > _CACHE_LIMIT:
            del self._responses[next(iter(self._responses))]
        if directory is not None:
            try:
                await asyncio.to_thread(
                    _write_stored, directory, _cache_file(directory, key), remembered
                )
            except OSError as exc:
                logger.warning("[workshop] cannot write cache: {}", exc)
        if changed:
            yield value

    def _install_state(
        self, mod: CatalogMod, installed: dict[str, list[ListedMod]]
    ) -> InstallState:
        local = installed.get(mod.id)
        if not local:
            return "missing"
        if self._manager is None:
            raise RuntimeError("Workshop catalog accessed before setup")
        synced = [
            timestamp
            for item in local
            if (timestamp := self._manager.last_synced(item)) is not None
        ]
        if synced and mod.updated_at is not None and max(synced) < mod.updated_at:
            return "outdated"
        return "installed"

    def install_state(self, mod: CatalogMod) -> InstallState:
        return self._install_state(mod, self._installed())

    async def installed_with_updates(self) -> list[CatalogMod]:
        client = self._catalog_client()
        items, _ = await client.mods_batch(list(self._installed()))
        return items

    async def plan(self, mod_ids: list[str], collection_ids: list[str]) -> DownloadPlan:
        resolved = await self._catalog_client().resolve(mod_ids, collection_ids)
        installed = self._installed()
        to_download: list[str] = []
        already_current: list[str] = []
        for id in resolved.mod_ids:
            mod = resolved.items.get(id)
            if not isinstance(mod, CatalogMod):
                raise CatalogError(
                    "The Workshop catalog returned an invalid download plan."
                )
            if self._install_state(mod, installed) == "installed":
                already_current.append(id)
            else:
                to_download.append(id)
        return DownloadPlan(
            to_download=to_download,
            already_current=already_current,
            unavailable_ids=resolved.unavailable_ids,
            incomplete_collection_ids=resolved.incomplete_collection_ids,
            titles={id: item.title for id, item in resolved.items.items()},
            is_complete=resolved.is_complete,
        )

    async def download(self, plan: DownloadPlan) -> DownloadResult:
        if self._manager is None:
            raise RuntimeError("Workshop catalog accessed before setup")
        return await self._manager.download_mods(plan.to_download)

    @property
    def queued_ids(self) -> frozenset[str]:
        return frozenset(self._queued)

    def enqueue(self, plan: DownloadPlan) -> None:
        """Queue *plan* behind any running download without waiting for it."""
        if self._manager is None:
            raise RuntimeError("Workshop catalog accessed before setup")
        active = self._manager.active_ids
        ids = [
            id for id in plan.to_download if id not in self._queued and id not in active
        ]
        if not ids:
            return
        self._queue.append(ids)
        self._queued.update(ids)
        self.queue_changed.emit(None)
        if self._runner is None or self._runner.done():
            self._runner = asyncio.ensure_future(self._drain())

    def cancel_queue(self) -> None:
        self._queue.clear()
        self._queued.clear()
        self.queue_changed.emit(None)

    async def _drain(self) -> None:
        manager = self._manager
        if manager is None:
            return
        while self._queue:
            ids = self._queue.pop(0)
            waited = manager.is_downloading
            while manager.is_downloading:
                self._idle.clear()
                await self._idle.wait()
            if waited and manager.cancelled:
                self.cancel_queue()
                return
            try:
                await manager.download_mods(ids)
            except (RuntimeError, ValueError) as exc:
                logger.warning("[workshop] queued download failed: {}", exc)
            self._queued.difference_update(ids)
            self.queue_changed.emit(None)
            if manager.cancelled:
                self.cancel_queue()
                return

    def _on_busy_changed(self, busy: bool) -> None:
        if not busy:
            self._idle.set()
