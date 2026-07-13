from __future__ import annotations

from abc import ABC, abstractmethod
from pathlib import Path

from pxmodrim.models.metadata.structures import ListedMod


class BaseModProvider(ABC):
    provider_id: str
    color: str = "#808080"

    def __init__(self, path: Path) -> None:
        self._path = path

    @abstractmethod
    async def discover(self, target_version: str) -> dict[str, ListedMod]:
        """Discover mods from this provider's path."""
        ...

    async def delete(self, mod_id: str) -> bool:
        return False

    async def update(self, mod_id: str) -> bool:
        return False
