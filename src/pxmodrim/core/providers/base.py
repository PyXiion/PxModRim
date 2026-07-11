from __future__ import annotations

import asyncio
from abc import ABC, abstractmethod
from pathlib import Path

from pxmodrim.models.metadata.parsing import create_listed_mod_from_path
from pxmodrim.models.metadata.structures import ListedMod
from pxmodrim.services.mod_discovery import scan_mod_directory


class BaseModProvider(ABC):
    provider_id: str

    def __init__(self, path: Path) -> None:
        self._path = path

    async def discover(self, target_version: str) -> dict[str, ListedMod]:
        def _scan() -> dict[str, ListedMod]:
            result: dict[str, ListedMod] = {}
            if not self._path.exists():
                return result
            for d in scan_mod_directory(self._path):
                _, mod = create_listed_mod_from_path(d, target_version)
                if self._should_include(mod):
                    mod.provider_id = self.provider_id
                    result[mod.uuid] = mod
            return result

        return await asyncio.to_thread(_scan)

    @abstractmethod
    def _should_include(self, mod: ListedMod) -> bool:
        """Filter which mods this provider should include."""
        ...

    async def delete(self, mod_id: str) -> bool:
        return False

    async def update(self, mod_id: str) -> bool:
        return False