from __future__ import annotations

import asyncio
from pathlib import Path

from pxmodrim.core.providers.base import BaseModProvider
from pxmodrim.models.metadata.parsing import create_listed_mod_from_path
from pxmodrim.models.metadata.structures import ListedMod
from pxmodrim.services.mod_discovery import scan_mod_directory


class SteamCmdModProvider(BaseModProvider):
    provider_id = "steam_cmd"

    def __init__(self, local_path: Path) -> None:
        self._local = local_path

    async def discover(self, target_version: str) -> dict[str, ListedMod]:
        def _scan() -> dict[str, ListedMod]:
            result: dict[str, ListedMod] = {}
            if not self._local.exists():
                return result
            for d in scan_mod_directory(self._local):
                _, mod = create_listed_mod_from_path(d, target_version)
                if (
                    mod.mod_path is None
                    or not (mod.mod_path / "About/PublishedFileId.txt").exists()
                ):
                    continue
                mod.provider_id = self.provider_id
                result[mod.uuid] = mod
            return result

        return await asyncio.to_thread(_scan)
