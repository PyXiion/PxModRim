from __future__ import annotations

import asyncio
from pathlib import Path
from typing import TYPE_CHECKING

from loguru import logger

from pxmodrim.core.models.metadata.parsing import create_listed_mod_from_path
from pxmodrim.core.models.metadata.structures import ListedMod
from pxmodrim.core.providers.base import BaseModProvider
from pxmodrim.core.services.mod_discovery import scan_mod_directory

if TYPE_CHECKING:
    from ttimer import Timer


class CoreModProvider(BaseModProvider):
    provider_id = "core"
    color = "#e67e22"

    def __init__(self, game_path: Path) -> None:
        """Point the provider at the game root (``Data/`` is resolved inside)."""
        self._game = game_path
        super().__init__(game_path)

    async def discover(
        self, target_version: str, timer: Timer | None = None
    ) -> dict[str, ListedMod]:
        """Scan ``Data/`` directory for core mods (runs off the main thread)."""

        def _scan(t: Timer | None) -> dict[str, ListedMod]:
            result: dict[str, ListedMod] = {}
            data_dir = self._game / "Data"

            logger.debug("CoreModProvider scanning: {}", data_dir)
            if data_dir.exists():
                with (t or Timer())("scan_dir"):
                    dirs = scan_mod_directory(data_dir)
                for d in dirs:
                    with (t or Timer())("parse_xml"):
                        _, mod = create_listed_mod_from_path(d, target_version)
                    logger.debug(
                        "CoreModProvider found: {} (uuid: {})", mod.name, mod.uuid
                    )
                    mod.provider_id = self.provider_id
                    result[mod.uuid] = mod

            return result

        discovered = await asyncio.to_thread(_scan, timer)
        logger.info("CoreModProvider discovered {} mods", len(discovered))
        return discovered
