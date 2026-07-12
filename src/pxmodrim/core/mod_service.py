from __future__ import annotations

from pathlib import Path

from pxmodrim._compat.mods_config import parse_mods_config, write_mods_config
from pxmodrim.core.context import CoreContext
from pxmodrim.core.providers.base import BaseModProvider
from pxmodrim.core.structures import CollectionStats
from pxmodrim.models.metadata.structures import (
    AboutXmlMod,
    CaseInsensitiveStr,
    ListedMod,
    ModsConfig,
)
from pxmodrim.services.mod_discovery import resolve_active_uuids


class ModService:
    def __init__(self, ctx: CoreContext, providers: list[BaseModProvider]) -> None:
        self._ctx = ctx
        self._providers = list(providers)

    def reset_providers(self, providers: list[BaseModProvider]) -> None:
        self._providers = list(providers)

    async def discover(self) -> None:
        all_mods: dict[str, ListedMod] = {}
        for p in self._providers:
            mods = await p.discover(self._ctx.config.target_version)
            all_mods.update(mods)
        active = resolve_active_uuids(all_mods, self._ctx.config.paths.config_folder)
        self._ctx.load(all_mods, active)

    def compute_stats(self, active_ids: set[str] | None = None) -> CollectionStats:
        return self._ctx.compute_stats(active_ids)

    async def save_active_layout(self, active_ids: list[str]) -> bool:
        cfg = self._ctx.config
        if not cfg.paths.config_folder:
            return False
        mods = self._ctx.all_mods
        package_ids: list[CaseInsensitiveStr] = []
        for uuid in active_ids:
            mod = mods.get(uuid)
            if isinstance(mod, AboutXmlMod):
                package_ids.append(mod.package_id)

        # Preserve knownExpansions from existing ModsConfig.xml
        config_path = Path(cfg.paths.config_folder) / "ModsConfig.xml"
        existing = parse_mods_config(config_path)
        known_expansions = existing.knownExpansions if existing else []

        data = ModsConfig(
            version=cfg.target_version,
            activeMods=package_ids,
            knownExpansions=known_expansions,
        )
        write_mods_config(config_path, data)
        return True
