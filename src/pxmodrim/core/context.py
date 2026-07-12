from __future__ import annotations

from typing import TYPE_CHECKING

from pxmodrim.core.structures import CollectionStats
from pxmodrim.models.metadata.structures import ListedMod
from pxmodrim.ui.mod_list_model import ModListModel

if TYPE_CHECKING:
    from pxmodrim._compat.config import AppConfig


class CoreContext:
    def __init__(self, cfg: "AppConfig") -> None:
        self._cfg = cfg
        self._mods: dict[str, ListedMod] = {}
        self._active_uuids: list[str] = []
        self._mod_list_model: ModListModel | None = None

    def load(self, mods: dict[str, ListedMod], active_uuids: list[str]) -> None:
        self._mods = dict(mods)
        self._active_uuids = list(active_uuids)

    def update_config(self, cfg: "AppConfig") -> None:
        self._cfg = cfg

    def set_mod_list_model(self, model: ModListModel) -> None:
        self._mod_list_model = model

    @property
    def all_mods(self) -> dict[str, ListedMod]:
        return dict(self._mods)

    @property
    def active_uuids(self) -> list[str]:
        return list(self._active_uuids)

    @property
    def config(self) -> "AppConfig":
        return self._cfg

    @property
    def mod_list_model(self) -> ModListModel | None:
        return self._mod_list_model

    def compute_stats(self, active_ids: list[str] | None = None) -> CollectionStats:
        ids = active_ids if active_ids is not None else self._active_uuids
        stats = CollectionStats()
        stats.total = len(self._mods)
        stats.active = len(ids)
        stats.inactive = stats.total - stats.active
        for m in self._mods.values():
            if not m.valid:
                stats.errors += 1
        return stats
