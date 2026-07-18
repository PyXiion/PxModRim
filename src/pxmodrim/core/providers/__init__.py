from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import TYPE_CHECKING

from pxmodrim.core.providers.base import BaseModProvider
from pxmodrim.core.providers.core import CoreModProvider
from pxmodrim.core.providers.local import LocalModProvider, SteamCmdModProvider

if TYPE_CHECKING:
    from pxmodrim.core.config import PathConfig


def create_providers(
    paths: PathConfig, pool: ThreadPoolExecutor | None = None
) -> list[BaseModProvider]:
    """Build the default provider list from the given path configuration."""
    providers: list[BaseModProvider] = []
    if paths.game:
        providers.append(CoreModProvider(Path(paths.game)))
    if paths.local:
        providers.append(LocalModProvider(Path(paths.local), pool=pool))
        providers.append(SteamCmdModProvider(Path(paths.local), pool=pool))
    return providers


__all__ = [
    "BaseModProvider",
    "CoreModProvider",
    "LocalModProvider",
    "SteamCmdModProvider",
    "create_providers",
]
