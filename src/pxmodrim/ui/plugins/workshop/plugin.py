from __future__ import annotations

from typing import TYPE_CHECKING, ClassVar

from pxmodrim.core.plugin import Plugin
from pxmodrim.ui.plugins.workshop.view import WorkshopViewPanel

if TYPE_CHECKING:
    from pxmodrim.ui.context import AppContext


class WorkshopUiPlugin(Plugin):
    name = "workshop_ui"
    dependencies: ClassVar[list[str]] = ["workshop_catalog", "downloads"]

    def setup(self, ctx: AppContext) -> None:  # type: ignore[override]
        ctx.add_rail_view(WorkshopViewPanel)
