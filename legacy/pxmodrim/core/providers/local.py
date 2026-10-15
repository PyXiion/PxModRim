from __future__ import annotations

import asyncio
import os
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


_in_flight_scans: dict[Path, asyncio.Task[dict[Path, tuple[Path, bool]]]] = {}


def _scan_with_pfid(path: Path) -> dict[Path, tuple[Path, bool]]:
    return {
        d: (a, os.path.exists(os.path.join(d, "About", "PublishedFileId.txt")))
        for d, a in scan_mod_directory(path).items()
    }


async def _shared_scan(path: Path) -> dict[Path, tuple[Path, bool]]:
    """Scan *path* once for concurrent callers.

    Local and downloaded providers share a root and run in parallel; joining an
    in-flight scan halves the disk work while never reusing a finished result.
    """
    task = _in_flight_scans.get(path)
    if task is None or task.get_loop() is not asyncio.get_running_loop():
        task = asyncio.ensure_future(asyncio.to_thread(_scan_with_pfid, path))
        _in_flight_scans[path] = task
        task.add_done_callback(
            lambda t: (
                _in_flight_scans.pop(path, None)
                if _in_flight_scans.get(path) is t
                else None
            )
        )
    return await asyncio.shield(task)


class LocalModProvider(BaseModProvider):
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
        force_reparse: bool = False,
    ) -> dict[str, ListedMod]:
        tm = timer or Timer()
        if not self._path.exists():
            logger.debug("LocalModProvider path does not exist: {}", self._path)
            return {}
        logger.debug("LocalModProvider scanning: {}", self._path)
        with tm("scan_dir"):
            scanned = await _shared_scan(self._path)
        filtered_dirs = {d: a for d, (a, has_pfid) in scanned.items() if not has_pfid}
        discovered = await self._load_mods(
            filtered_dirs,
            target_version,
            timer=tm,
            metadata_cache=metadata_cache,
            force_reparse=force_reparse,
        )
        logger.info("LocalModProvider discovered {} mods", len(discovered))
        return discovered


class DownloadedModProvider(BaseModProvider):
    provider_id = "downloaded"
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
        force_reparse: bool = False,
    ) -> dict[str, ListedMod]:
        tm = timer or Timer()
        if not self._path.exists():
            logger.debug("DownloadedModProvider path does not exist: {}", self._path)
            return {}
        logger.debug("DownloadedModProvider scanning: {}", self._path)
        with tm("scan_dir"):
            scanned = await _shared_scan(self._path)
        filtered_dirs = {d: a for d, (a, has_pfid) in scanned.items() if has_pfid}
        discovered = await self._load_mods(
            filtered_dirs,
            target_version,
            timer=tm,
            metadata_cache=metadata_cache,
            force_reparse=force_reparse,
        )
        logger.info("DownloadedModProvider discovered {} mods", len(discovered))
        return discovered


class SteamWorkshopModProvider(DownloadedModProvider):
    provider_id = "steam"
