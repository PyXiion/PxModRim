from __future__ import annotations

import asyncio
from abc import ABC, abstractmethod
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import TYPE_CHECKING

from ttimer import Timer

from pxmodrim.core.models.metadata.parsing import create_listed_mod_from_path
from pxmodrim.core.models.metadata.structures import AboutXmlMod, ListedMod

if TYPE_CHECKING:
    from pxmodrim.core.services.metadata_cache import MetadataCache


class BaseModProvider(ABC):
    """Abstract mod provider. Subclasses implement :meth:`discover`."""

    provider_id: str
    color: str = "#808080"

    def __init__(
        self,
        path: Path,
        pool: ThreadPoolExecutor | None = None,
        metadata_cache: MetadataCache | None = None,
    ) -> None:
        """Store the root filesystem path this provider scans."""
        self._path = path
        self._pool = pool
        self._metadata_cache = metadata_cache

    @abstractmethod
    async def discover(
        self,
        target_version: str,
        timer: Timer | None = None,
        metadata_cache: MetadataCache | None = None,
    ) -> dict[str, ListedMod]:
        """Discover mods from this provider's path."""
        ...

    async def _load_mods(
        self,
        dirs: dict[Path, Path],
        target_version: str,
        timer: Timer | None = None,
        metadata_cache: MetadataCache | None = None,
    ) -> dict[str, ListedMod]:
        tm = timer or Timer()
        cache = metadata_cache if metadata_cache is not None else self._metadata_cache

        candidates: list[tuple[Path, Path, float, int]] = []
        for d, a in dirs.items():
            try:
                st = a.stat()
                candidates.append((d, a, st.st_mtime, st.st_size))
            except OSError:
                continue

        result: dict[str, ListedMod] = {}
        missing: list[tuple[Path, Path, float, int]] = []

        if cache is not None:
            with tm("cache_lookup"):
                cached_mods, missing = await cache.get_mods(candidates, target_version)
                for mod in cached_mods.values():
                    mod.provider_id = self.provider_id
                    result[mod.uuid] = mod
        else:
            missing = candidates

        if missing:
            with tm("parse_xml"):
                newly_parsed = await self._parse_candidates(missing, target_version)
                cache_items = [
                    (m, a, mt, sz)
                    for m, a, mt, sz in newly_parsed
                    if isinstance(m, AboutXmlMod)
                ]
                if cache is not None and cache_items:
                    await cache.put_mods(cache_items, target_version)
                for mod, _, _, _ in newly_parsed:
                    mod.provider_id = self.provider_id
                    result[mod.uuid] = mod

        return result

    async def _parse_candidates(
        self,
        missing: list[tuple[Path, Path, float, int]],
        target_version: str,
    ) -> list[tuple[ListedMod, Path, float, int]]:
        def _parse_one(
            item: tuple[Path, Path, float, int],
        ) -> tuple[ListedMod, Path, float, int]:
            d, a, mtime, size = item
            _, mod = create_listed_mod_from_path(d, target_version, about_xml_path=a)
            return mod, a, mtime, size

        def _parse_all() -> list[tuple[ListedMod, Path, float, int]]:
            pool = self._pool
            if pool is not None and len(missing) > 1:
                futures = [pool.submit(_parse_one, item) for item in missing]
                return [f.result() for f in as_completed(futures)]
            return [_parse_one(item) for item in missing]

        return await asyncio.to_thread(_parse_all)
