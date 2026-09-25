from __future__ import annotations

import asyncio
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import TYPE_CHECKING

from loguru import logger
from ttimer import Timer

from pxmodrim.core.models.metadata.structures import ListedMod
from pxmodrim.core.providers.base import BaseModProvider
from pxmodrim.core.services.mod_discovery import scan_mod_directory

if TYPE_CHECKING:
    from pxmodrim.core.services.metadata_cache import MetadataCache


class LocalModProvider(BaseModProvider):
    """Provider for local (non-Steam) mods."""

    provider_id = "local"
    color = "#2ecc71"

    def __init__(
        self,
        local_path: Path,
        pool: ThreadPoolExecutor | None = None,
        metadata_cache: MetadataCache | None = None,
    ) -> None:
        super().__init__(local_path, pool=pool, metadata_cache=metadata_cache)

    async def discover(
        self,
        target_version: str,
        timer: Timer | None = None,
        metadata_cache: MetadataCache | None = None,
    ) -> dict[str, ListedMod]:
        """Scan local path for mods lacking `PublishedFileId.txt` (off main thread)."""
        tm = timer or Timer()
        if not self._path.exists():
            logger.debug("LocalModProvider path does not exist: {}", self._path)
            return {}
        logger.debug("LocalModProvider scanning: {}", self._path)
        with tm("scan_dir"):
            dirs = await asyncio.to_thread(scan_mod_directory, self._path)

        def _filter(d: Path) -> bool:
            return not (d / "About/PublishedFileId.txt").exists()

        filtered_dirs = await asyncio.to_thread(
            lambda: {d: a for d, a in dirs.items() if _filter(d)}
        )
        discovered = await self._load_mods(
            filtered_dirs, target_version, timer=tm, metadata_cache=metadata_cache
        )
        logger.info("LocalModProvider discovered {} mods", len(discovered))
        return discovered


class SteamCmdModProvider(BaseModProvider):
    """Provider for Steam CMD / workshop mods - only mods with PublishedFileId.txt."""

    provider_id = "steam_cmd"
    color = "#3498db"

    def __init__(
        self,
        local_path: Path,
        pool: ThreadPoolExecutor | None = None,
        metadata_cache: MetadataCache | None = None,
    ) -> None:
        super().__init__(local_path, pool=pool, metadata_cache=metadata_cache)

    async def discover(
        self,
        target_version: str,
        timer: Timer | None = None,
        metadata_cache: MetadataCache | None = None,
    ) -> dict[str, ListedMod]:
        """Scan local path for mods with ``PublishedFileId.txt`` (off main thread)."""
        tm = timer or Timer()
        if not self._path.exists():
            logger.debug("SteamCmdModProvider path does not exist: {}", self._path)
            return {}
        logger.debug("SteamCmdModProvider scanning: {}", self._path)
        with tm("scan_dir"):
            dirs = await asyncio.to_thread(scan_mod_directory, self._path)

        def _filter(d: Path) -> bool:
            return (d / "About/PublishedFileId.txt").exists()

        filtered_dirs = await asyncio.to_thread(
            lambda: {d: a for d, a in dirs.items() if _filter(d)}
        )
        discovered = await self._load_mods(
            filtered_dirs, target_version, timer=tm, metadata_cache=metadata_cache
        )
        logger.info("SteamCmdModProvider discovered {} mods", len(discovered))
        return discovered


class SteamWorkshopModProvider(SteamCmdModProvider):
    """Provider for mods installed by the Steam client."""

    provider_id = "steam"
