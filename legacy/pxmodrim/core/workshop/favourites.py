from __future__ import annotations

from typing import TYPE_CHECKING

import msgspec

from pxmodrim.core.plugin_config import PluginConfig
from pxmodrim.core.workshop.types import CatalogCollection

if TYPE_CHECKING:
    from pxmodrim.core.config import ConfigService
    from pxmodrim.core.events import Event


class _Stored(msgspec.Struct, frozen=True):
    collections: list[CatalogCollection] = msgspec.field(default_factory=list)


class FavouriteCollections:
    """Collections the user starred, newest first.

    Each entry is the last catalog snapshot of the collection, so the list shows
    without a network round trip and survives the catalog dropping it.
    """

    __slots__ = ("_config", "_ids")

    def __init__(self, service: ConfigService | None) -> None:
        self._config = PluginConfig(service, "workshop_favourites", _Stored)
        self._ids = {item.id for item in self._config.value.collections}

    @property
    def changed(self) -> Event[_Stored]:
        return self._config.changed

    @property
    def items(self) -> list[CatalogCollection]:
        return list(self._config.value.collections)

    def __contains__(self, collection_id: str) -> bool:
        return collection_id in self._ids

    def set(self, collection: CatalogCollection, favourite: bool) -> None:
        if (collection.id in self._ids) == favourite:
            return
        rest = [c for c in self._config.value.collections if c.id != collection.id]
        self._save([collection, *rest] if favourite else rest)

    def remember(self, collection: CatalogCollection) -> None:
        """Refresh the stored snapshot of a starred collection."""
        if collection.id not in self._ids:
            return
        stored = self._config.value.collections
        if collection in stored:
            return
        self._save([collection if c.id == collection.id else c for c in stored])

    def _save(self, collections: list[CatalogCollection]) -> None:
        self._ids = {item.id for item in collections}
        self._config.update(_Stored(collections))
