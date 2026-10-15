from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING, ClassVar, cast

from pxmodrim.core.plugin import Plugin
from pxmodrim.ui.settings_section import PluginConfigSection

if TYPE_CHECKING:
    from PySide6.QtCore import QObject

    from pxmodrim.core.downloads.steam import SteamDownloader
    from pxmodrim.ui.context import AppContext


class SteamSettingsSection(PluginConfigSection):
    title = "Steam Workshop"
    source = Path(__file__).with_name("SteamSettings.qml")


class SteamDownloaderUiPlugin(Plugin):
    """Settings for the Steam downloader; imports nothing from it at runtime."""

    name = "steam_downloader_ui"
    dependencies: ClassVar[list[str]] = ["steam_downloader"]

    def setup(self, ctx: AppContext) -> None:  # type: ignore[override]
        downloader = cast("SteamDownloader", ctx.core.plugins.get("steam_downloader"))

        def section(parent: QObject) -> SteamSettingsSection:
            return SteamSettingsSection(downloader.settings, parent)

        ctx.add_settings_section(section)
