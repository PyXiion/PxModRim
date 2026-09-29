from __future__ import annotations

from typing import TYPE_CHECKING, ClassVar

from pxmodrim.core.plugin import Plugin
from pxmodrim.ui.plugins.downloads.view import DownloadsViewPanel

if TYPE_CHECKING:
    from pxmodrim.ui.context import AppContext


class DownloadsUiPlugin(Plugin):
    name = "downloads_ui"
    dependencies: ClassVar[list[str]] = ["workshop_download"]

    def setup(self, ctx: AppContext) -> None:  # type: ignore[override]
        ctx.add_rail_view(DownloadsViewPanel)
