from __future__ import annotations

from pxmodrim.ui.views.base import BaseViewPanel
from pxmodrim.ui.views.mods_view import ModsViewPanel
from pxmodrim.ui.views.steam_workshop_view import SteamWorkshopViewPanel

__all__ = [
    "BaseViewPanel",
    "ModsViewPanel",
    "SteamWorkshopViewPanel",
    "builtin_views",
]


def builtin_views() -> list[type[BaseViewPanel]]:
    """Ordered list of built-in rail views. Add new views here only."""
    return [ModsViewPanel, SteamWorkshopViewPanel]
