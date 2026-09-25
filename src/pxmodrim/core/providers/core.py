from __future__ import annotations

import asyncio
from pathlib import Path
from typing import TYPE_CHECKING

from loguru import logger
from ttimer import Timer

from pxmodrim.core.models.metadata.structures import ListedMod
from pxmodrim.core.providers.base import BaseModProvider
from pxmodrim.core.services.mod_discovery import scan_mod_directory

if TYPE_CHECKING:
    from pxmodrim.core.services.metadata_cache import MetadataCache


class CoreModProvider(BaseModProvider):
    provider_id = "core"
    color = "#e67e22"

    def __init__(
        self,
        game_path: Path,
        metadata_cache: MetadataCache | None = None,
    ) -> None:
        """Point the provider at the game root (``Data/`` is resolved inside)."""
        self._game = game_path
        super().__init__(game_path, metadata_cache=metadata_cache)

    async def discover(
        self,
        target_version: str,
        timer: Timer | None = None,
        metadata_cache: MetadataCache | None = None,
    ) -> dict[str, ListedMod]:
        """Scan ``Data/`` directory for core mods (runs off the main thread)."""
        tm = timer or Timer()
        data_dir = self._game / "Data"
        logger.debug("CoreModProvider scanning: {}", data_dir)
        if not data_dir.exists():
            return {}

        with tm("scan_dir"):
            dirs = await asyncio.to_thread(scan_mod_directory, data_dir)

        discovered = await self._load_mods(
            dirs, target_version, timer=tm, metadata_cache=metadata_cache
        )
        for mod in discovered.values():
            logger.debug("CoreModProvider found: {} (uuid: {})", mod.name, mod.uuid)
        logger.info("CoreModProvider discovered {} mods", len(discovered))
        return discovered
