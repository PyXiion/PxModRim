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
    """Orchestrates mod discovery and lifecycle across a set of providers."""

    def __init__(self, ctx: CoreContext, providers: list[BaseModProvider]) -> None:
        self._ctx = ctx
        self._providers: dict[str, BaseModProvider] = {
            p.provider_id: p for p in providers
        }

    def reset_providers(self, providers: list[BaseModProvider]) -> None:
        """Replace the current provider set with a new list."""
        self._providers = {p.provider_id: p for p in providers}

    def get_provider(self, provider_id: str) -> BaseModProvider | None:
        """Retrieve a provider by its ``provider_id``, or ``None``."""
        return self._providers.get(provider_id)

    @property
    def provider_colors(self) -> dict[str, str]:
        return {pid: p.color for pid, p in self._providers.items()}

    async def discover(self) -> None:
        """Run all providers' discovery in sequence and load results into context."""
        all_mods: dict[str, ListedMod] = {}
        for p in self._providers.values():
            mods = await p.discover(self._ctx.target_version)
            all_mods.update(mods)
        active = resolve_active_uuids(all_mods, self._ctx.config.paths.config_folder)
        self._ctx.load(all_mods, active)

    def compute_stats(self, active_ids: list[str] | None = None) -> CollectionStats:
        return self._ctx.compute_stats(active_ids)

    async def save_active_layout(self, active_ids: list[str]) -> bool:
        """Persist the given active UUIDs as a ModsConfig.xml file."""
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
            version=self._ctx.target_version,
            activeMods=package_ids,
            knownExpansions=known_expansions,
        )
        write_mods_config(config_path, data)
        return True
