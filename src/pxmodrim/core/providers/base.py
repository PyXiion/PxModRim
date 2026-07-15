from __future__ import annotations

from abc import ABC, abstractmethod
from pathlib import Path
from typing import TYPE_CHECKING

from pxmodrim.core.models.metadata.structures import ListedMod

if TYPE_CHECKING:
    from ttimer import Timer


class BaseModProvider(ABC):
    """Abstract mod provider. Subclasses implement :meth:`discover`."""

    provider_id: str
    color: str = "#808080"

    def __init__(self, path: Path) -> None:
        """Store the root filesystem path this provider scans."""
        self._path = path

    @abstractmethod
    async def discover(
        self, target_version: str, timer: Timer | None = None
    ) -> dict[str, ListedMod]:
        """Discover mods from this provider's path."""
        ...

    async def delete(self, mod_id: str) -> bool:
        """Delete the mod identified by *mod_id*. Subclasses may override."""
        return False

    async def update(self, mod_id: str) -> bool:
        """Update the mod identified by *mod_id*. Subclasses may override."""
        return False
