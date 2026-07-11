from __future__ import annotations

import asyncio
from pathlib import Path

from pxmodrim.core.providers.base import BaseModProvider
from pxmodrim.models.metadata.parsing import create_listed_mod_from_path
from pxmodrim.models.metadata.structures import ListedMod
from pxmodrim.services.mod_discovery import scan_mod_directory


class CoreModProvider(BaseModProvider):
    provider_id = "core"

    def __init__(self, game_path: Path) -> None:
        self._game = game_path

    async def discover(self, target_version: str) -> dict[str, ListedMod]:
        def _scan() -> dict[str, ListedMod]:
            result: dict[str, ListedMod] = {}
            mods_dir = self._game / "Mods"
            data_dir = self._game / "Data"

            if mods_dir.exists():
                for d in scan_mod_directory(mods_dir):
                    _, mod = create_listed_mod_from_path(d, target_version)
                    mod.provider_id = self.provider_id
                    result[mod.uuid] = mod

            if data_dir.exists():
                for d in data_dir.iterdir():
                    if d.is_dir():
                        _, mod = create_listed_mod_from_path(d, target_version)
                        mod.provider_id = self.provider_id
                        result[mod.uuid] = mod

            return result

        return await asyncio.to_thread(_scan)
