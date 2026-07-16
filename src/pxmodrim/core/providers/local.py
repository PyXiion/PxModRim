from __future__ import annotations

import asyncio
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

from loguru import logger
from ttimer import Timer

from pxmodrim.core.models.metadata.parsing import create_listed_mod_from_path
from pxmodrim.core.models.metadata.structures import ListedMod
from pxmodrim.core.providers.base import BaseModProvider
from pxmodrim.core.services.mod_discovery import scan_mod_directory


class LocalModProvider(BaseModProvider):
    """Provider for local (non-Steam) mods."""

    provider_id = "local"
    color = "#2ecc71"

    def __init__(
        self, local_path: Path, pool: ThreadPoolExecutor | None = None
    ) -> None:
        super().__init__(local_path, pool=pool)

    async def discover(
        self, target_version: str, timer: Timer | None = None
    ) -> dict[str, ListedMod]:
        """Scan local path for mods lacking `PublishedFileId.txt` (off main thread)."""

        def _scan(t: Timer | None) -> dict[str, ListedMod]:
            tm = t or Timer()
            if not self._path.exists():
                logger.debug("LocalModProvider path does not exist: {}", self._path)
                return {}
            logger.debug("LocalModProvider scanning: {}", self._path)
            with tm("scan_dir"):
                dirs = scan_mod_directory(self._path)

            def _keep(d: Path) -> ListedMod | None:
                if (d / "About/PublishedFileId.txt").exists():
                    return None
                _, mod = create_listed_mod_from_path(d, target_version)
                mod.provider_id = self.provider_id
                return mod

            pool = self._pool
            if pool is not None:
                with tm("process_mods"):
                    futures = {pool.submit(_keep, d): d for d in dirs}
                    result: dict[str, ListedMod] = {}
                    for f in as_completed(futures):
                        mod = f.result()
                        if mod is not None:
                            result[mod.uuid] = mod
            else:
                with tm("process_mods"):
                    result = {}
                    for d in dirs:
                        mod = _keep(d)
                        if mod is not None:
                            result[mod.uuid] = mod
            return result

        discovered = await asyncio.to_thread(_scan, timer)
        logger.info("LocalModProvider discovered {} mods", len(discovered))
        return discovered


class SteamCmdModProvider(BaseModProvider):
    """Provider for Steam CMD / workshop mods - only mods with PublishedFileId.txt."""

    provider_id = "steam_cmd"
    color = "#3498db"

    def __init__(
        self, local_path: Path, pool: ThreadPoolExecutor | None = None
    ) -> None:
        super().__init__(local_path, pool=pool)

    async def discover(
        self, target_version: str, timer: Timer | None = None
    ) -> dict[str, ListedMod]:
        """Scan local path for mods with ``PublishedFileId.txt`` (off main thread)."""

        def _scan(t: Timer | None) -> dict[str, ListedMod]:
            tm = t or Timer()
            if not self._path.exists():
                logger.debug("SteamCmdModProvider path does not exist: {}", self._path)
                return {}
            logger.debug("SteamCmdModProvider scanning: {}", self._path)
            with tm("scan_dir"):
                dirs = scan_mod_directory(self._path)

            def _keep(d: Path) -> ListedMod | None:
                if not (d / "About/PublishedFileId.txt").exists():
                    return None
                _, mod = create_listed_mod_from_path(d, target_version)
                mod.provider_id = self.provider_id
                return mod

            pool = self._pool
            if pool is not None:
                with tm("process_mods"):
                    futures = {pool.submit(_keep, d): d for d in dirs}
                    result: dict[str, ListedMod] = {}
                    for f in as_completed(futures):
                        mod = f.result()
                        if mod is not None:
                            result[mod.uuid] = mod
            else:
                with tm("process_mods"):
                    result = {}
                    for d in dirs:
                        mod = _keep(d)
                        if mod is not None:
                            result[mod.uuid] = mod
            return result

        discovered = await asyncio.to_thread(_scan, timer)
        logger.info("SteamCmdModProvider discovered {} mods", len(discovered))
        return discovered
