from __future__ import annotations

import asyncio
from pathlib import Path

from pxmodrim.core.providers.base import BaseModProvider
from pxmodrim.models.metadata.parsing import create_listed_mod_from_path
from pxmodrim.models.metadata.structures import ListedMod
from pxmodrim.services.mod_discovery import scan_mod_directory


class LocalModProvider(BaseModProvider):
    """Unified provider for local mods - handles both non-Steam and Steam CMD mods in one scan."""
    provider_id = "local"

    def __init__(self, local_path: Path) -> None:
        super().__init__(local_path)

    async def discover(self, target_version: str) -> dict[str, ListedMod]:
        def _scan() -> dict[str, ListedMod]:
            result: dict[str, ListedMod] = {}
            if not self._path.exists():
                return result
            for d in scan_mod_directory(self._path):
                _, mod = create_listed_mod_from_path(d, target_version)
                has_pfid = (
                    mod.mod_path is not None
                    and (mod.mod_path / "About/PublishedFileId.txt").exists()
                )
                # Local provider handles non-Steam mods
                if not has_pfid:
                    mod.provider_id = self.provider_id
                    result[mod.uuid] = mod
            return result

        return await asyncio.to_thread(_scan)

    def _should_include(self, mod: ListedMod) -> bool:
        # Not used - discover() is overridden
        return False


class SteamCmdModProvider(BaseModProvider):
    """Provider for Steam CMD / workshop mods - only includes mods with PublishedFileId.txt."""
    provider_id = "steam_cmd"

    def __init__(self, local_path: Path) -> None:
        super().__init__(local_path)

    async def discover(self, target_version: str) -> dict[str, ListedMod]:
        def _scan() -> dict[str, ListedMod]:
            result: dict[str, ListedMod] = {}
            if not self._path.exists():
                return result
            for d in scan_mod_directory(self._path):
                _, mod = create_listed_mod_from_path(d, target_version)
                has_pfid = (
                    mod.mod_path is not None
                    and (mod.mod_path / "About/PublishedFileId.txt").exists()
                )
                # Steam provider handles only Steam mods
                if has_pfid:
                    mod.provider_id = self.provider_id
                    result[mod.uuid] = mod
            return result

        return await asyncio.to_thread(_scan)

    def _should_include(self, mod: ListedMod) -> bool:
        # Not used - discover() is overridden
        return False