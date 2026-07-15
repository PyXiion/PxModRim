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


class LocalModProvider(BaseModProvider):
    """Provider for local (non-Steam) mods."""

    provider_id = "local"
    color = "#2ecc71"

    def __init__(self, local_path: Path) -> None:
        super().__init__(local_path)

    async def discover(
        self, target_version: str, timer: Timer | None = None
    ) -> dict[str, ListedMod]:
        """Scan local path for mods lacking `PublishedFileId.txt` (off main thread)."""

        def _scan(t: Timer | None) -> dict[str, ListedMod]:
            result: dict[str, ListedMod] = {}
            if not self._path.exists():
                logger.debug("LocalModProvider path does not exist: {}", self._path)
                return result
            logger.debug("LocalModProvider scanning: {}", self._path)
            with (t or Timer())("scan_dir"):
                dirs = scan_mod_directory(self._path)
            for d in dirs:
                with (t or Timer())("parse_xml"):
                    _, mod = create_listed_mod_from_path(d, target_version)
                has_pfid = (
                    mod.mod_path is not None
                    and (mod.mod_path / "About/PublishedFileId.txt").exists()
                )
                # Local provider handles non-Steam mods
                if not has_pfid:
                    logger.trace(
                        "LocalModProvider found: {} (uuid: {})", mod.name, mod.uuid
                    )
                    mod.provider_id = self.provider_id
                    result[mod.uuid] = mod
                else:
                    logger.trace("LocalModProvider skipping Steam mod: {}", mod.name)
            return result

        discovered = await asyncio.to_thread(_scan, timer)
        logger.info("LocalModProvider discovered {} mods", len(discovered))
        return discovered


class SteamCmdModProvider(BaseModProvider):
    """Provider for Steam CMD / workshop mods - only mods with PublishedFileId.txt."""

    provider_id = "steam_cmd"
    color = "#3498db"

    def __init__(self, local_path: Path) -> None:
        super().__init__(local_path)

    async def discover(
        self, target_version: str, timer: Timer | None = None
    ) -> dict[str, ListedMod]:
        """Scan local path for mods with ``PublishedFileId.txt`` (off main thread)."""

        def _scan(t: Timer | None) -> dict[str, ListedMod]:
            result: dict[str, ListedMod] = {}
            if not self._path.exists():
                logger.debug("SteamCmdModProvider path does not exist: {}", self._path)
                return result
            logger.debug("SteamCmdModProvider scanning: {}", self._path)
            with (t or Timer())("scan_dir"):
                dirs = scan_mod_directory(self._path)
            for d in dirs:
                with (t or Timer())("parse_xml"):
                    _, mod = create_listed_mod_from_path(d, target_version)
                has_pfid = (
                    mod.mod_path is not None
                    and (mod.mod_path / "About/PublishedFileId.txt").exists()
                )
                # Steam provider handles only Steam mods
                if has_pfid:
                    logger.trace(
                        "SteamCmdModProvider found: {} (uuid: {})", mod.name, mod.uuid
                    )
                    mod.provider_id = self.provider_id
                    result[mod.uuid] = mod
                else:
                    logger.trace(
                        "SteamCmdModProvider skipping non-Steam mod: {}", mod.name
                    )
            return result

        discovered = await asyncio.to_thread(_scan, timer)
        logger.info("SteamCmdModProvider discovered {} mods", len(discovered))
        return discovered
