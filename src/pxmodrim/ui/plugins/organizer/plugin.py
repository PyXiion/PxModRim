from __future__ import annotations

from typing import TYPE_CHECKING, ClassVar

from pxmodrim.core.plugin import Plugin
from pxmodrim.ui.plugins.organizer.view import OrganizerViewPanel

if TYPE_CHECKING:
    from pxmodrim.ui.context import AppContext


class OrganizerUiPlugin(Plugin):
    name = "organizer_ui"
    dependencies: ClassVar[list[str]] = ["organizer"]

    def setup(self, ctx: AppContext) -> None:  # type: ignore[override]
        ctx.add_rail_view(OrganizerViewPanel, position=0)
