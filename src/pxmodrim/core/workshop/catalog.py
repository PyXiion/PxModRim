from __future__ import annotations

from collections.abc import Callable
from typing import TYPE_CHECKING, ClassVar

from pxmodrim.core.downloads.manager import DownloadManager, download_manager
from pxmodrim.core.events import Event
from pxmodrim.core.plugin import Plugin
from pxmodrim.core.workshop.client import CatalogClient, CatalogError
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
    from pxmodrim.core.config import AppConfig
    from pxmodrim.core.context import CoreContext
    from pxmodrim.core.downloads.types import DownloadResult
    from pxmodrim.core.models.metadata.structures import ListedMod


class WorkshopCatalog(Plugin):
    name = "workshop_catalog"
    dependencies: ClassVar[list[str]] = ["downloads"]

    def __init__(
        self,
        config_accessor: Callable[[], AppConfig],
        client_factory: Callable[[str], CatalogClient] = CatalogClient,
    ) -> None:
        self.installed_changed: Event[None] = Event()
        self.catalog_url_changed: Event[str] = Event()
        self._config = config_accessor
        self._client_factory = client_factory
        self._base_url = config_accessor().workshop_catalog_url.strip().rstrip("/")
        self._client: CatalogClient | None = None
        self._clients: list[CatalogClient] = []
        self._ctx: CoreContext | None = None
        self._manager: DownloadManager | None = None

    def setup(self, ctx: CoreContext) -> None:
        self._ctx = ctx
        self._manager = download_manager(ctx)
        ctx.mod_service.mods_changed.connect(self._on_installed_changed)
        self._manager.download_finished.connect(self._on_download_finished)
        ctx.config_changed.connect(self._on_config_changed)

    def _on_installed_changed(self, _: None) -> None:
        self.installed_changed.emit(None)

    def _on_download_finished(self, _: DownloadResult) -> None:
        self.installed_changed.emit(None)

    def _on_config_changed(self, config: AppConfig) -> None:
        self._change_url(config.workshop_catalog_url)
        self.installed_changed.emit(None)

    def _change_url(self, url: str) -> None:
        url = url.strip().rstrip("/")
        if self._base_url == url:
            return
        self._base_url = url
        self._client = None
        self.catalog_url_changed.emit(url)

    def set_base_url(self, url: str) -> None:
        self._config().workshop_catalog_url = url.strip().rstrip("/")
        self._change_url(url)
        if self._ctx is not None and self._ctx.has_config_service:
            self._ctx.config_service.save("config.json", self._config())

    @property
    def configured(self) -> bool:
        return bool(self._config().workshop_catalog_url.strip())

    def _catalog_client(self) -> CatalogClient:
        self._change_url(self._config().workshop_catalog_url)
        if not self.configured:
            raise CatalogError("Workshop catalog is disabled.")
        if self._client is None:
            self._client = self._client_factory(self._base_url)
            # Keep replaced clients alive until shutdown so in-flight requests finish.
            self._clients.append(self._client)
        return self._client

    async def shutdown(self) -> None:
        if self._ctx is not None:
            self._ctx.mod_service.mods_changed.disconnect(self._on_installed_changed)
            self._ctx.config_changed.disconnect(self._on_config_changed)
        if self._manager is not None:
            self._manager.download_finished.disconnect(self._on_download_finished)
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
        installed: dict[str, list[ListedMod]] = {}
        for mod in self._ctx.all_mods.values():
            id = mod.published_file_id
            if id is not None:
                installed.setdefault(id, []).append(mod)
        return installed

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
