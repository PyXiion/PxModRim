from __future__ import annotations

import asyncio
from pathlib import Path

from loguru import logger

from pxmodrim.core.providers.base import BaseModProvider
from pxmodrim.models.metadata.parsing import create_listed_mod_from_path
from pxmodrim.models.metadata.structures import ListedMod
from pxmodrim.services.mod_discovery import scan_mod_directory


class CoreModProvider(BaseModProvider):
    provider_id = "core"
    color = "#e67e22"

    def __init__(self, game_path: Path) -> None:
        self._game = game_path
        super().__init__(game_path)

    async def discover(self, target_version: str) -> dict[str, ListedMod]:
        def _scan() -> dict[str, ListedMod]:
            result: dict[str, ListedMod] = {}
            data_dir = self._game / "Data"

            logger.debug("CoreModProvider scanning: {}", data_dir)
            if data_dir.exists():
                for d in scan_mod_directory(data_dir):
                    _, mod = create_listed_mod_from_path(d, target_version)
                    logger.debug(
                        "CoreModProvider found: {} (uuid: {})", mod.name, mod.uuid
                    )
                    mod.provider_id = self.provider_id
                    result[mod.uuid] = mod

            return result

        discovered = await asyncio.to_thread(_scan)
        logger.info("CoreModProvider discovered {} mods", len(discovered))
        return discovered
