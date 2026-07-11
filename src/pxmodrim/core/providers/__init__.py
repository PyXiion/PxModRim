from __future__ import annotations

from pathlib import Path

from pxmodrim._compat.config import PathConfig
from pxmodrim.core.providers.base import BaseModProvider
from pxmodrim.core.providers.core import CoreModProvider
from pxmodrim.core.providers.local import LocalModProvider
from pxmodrim.core.providers.steam_cmd import SteamCmdModProvider


def create_providers(paths: PathConfig) -> list[BaseModProvider]:
    providers: list[BaseModProvider] = []
    if paths.game:
        providers.append(CoreModProvider(Path(paths.game)))
    if paths.local:
        providers.append(LocalModProvider(Path(paths.local)))
        providers.append(SteamCmdModProvider(Path(paths.local)))
    return providers


__all__ = [
    "BaseModProvider",
    "CoreModProvider",
    "LocalModProvider",
    "SteamCmdModProvider",
    "create_providers",
]
