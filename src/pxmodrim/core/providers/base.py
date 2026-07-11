from __future__ import annotations

from abc import ABC, abstractmethod

from pxmodrim.models.metadata.structures import ListedMod


class BaseModProvider(ABC):
    provider_id: str

    @abstractmethod
    async def discover(self, target_version: str) -> dict[str, ListedMod]: ...

    async def delete(self, mod_id: str) -> bool:
        return False

    async def update(self, mod_id: str) -> bool:
        return False
