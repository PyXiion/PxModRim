from __future__ import annotations

from pxmodrim._compat.config import AppConfig
from pxmodrim.core.structures import CollectionStats
from pxmodrim.models.metadata.structures import ListedMod


class CoreContext:
    def __init__(self, cfg: AppConfig) -> None:
        self._cfg = cfg
        self._mods: dict[str, ListedMod] = {}
        self._active_uuids: set[str] = set()

    def load(self, mods: dict[str, ListedMod], active_uuids: set[str]) -> None:
        self._mods = dict(mods)
        self._active_uuids = set(active_uuids)

    def update_config(self, cfg: AppConfig) -> None:
        self._cfg = cfg

    @property
    def all_mods(self) -> dict[str, ListedMod]:
        return dict(self._mods)

    @property
    def active_uuids(self) -> set[str]:
        return set(self._active_uuids)

    @property
    def config(self) -> AppConfig:
        return self._cfg

    def compute_stats(self, active_ids: set[str] | None = None) -> CollectionStats:
        ids = active_ids if active_ids is not None else self._active_uuids
        stats = CollectionStats()
        stats.total = len(self._mods)
        stats.active = len(ids)
        stats.inactive = stats.total - stats.active
        for m in self._mods.values():
            if not m.valid:
                stats.errors += 1
        return stats
