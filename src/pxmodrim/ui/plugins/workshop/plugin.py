from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING, ClassVar, cast

from pxmodrim.core.plugin import Plugin
from pxmodrim.core.workshop import WorkshopCatalog
from pxmodrim.ui.plugins.workshop.view import WorkshopViewPanel
from pxmodrim.ui.settings_section import PluginConfigSection

if TYPE_CHECKING:
    from PySide6.QtCore import QObject

    from pxmodrim.ui.context import AppContext


class CatalogSettingsSection(PluginConfigSection):
    title = "Workshop catalog"
    source = Path(__file__).with_name("CatalogSettings.qml")


class WorkshopUiPlugin(Plugin):
    name = "workshop_ui"
    dependencies: ClassVar[list[str]] = ["workshop_catalog", "downloads"]

    def setup(self, ctx: AppContext) -> None:  # type: ignore[override]
        ctx.add_rail_view(WorkshopViewPanel)
        catalog = cast(WorkshopCatalog, ctx.core.plugins.get("workshop_catalog"))

        def section(parent: QObject) -> CatalogSettingsSection:
            return CatalogSettingsSection(catalog.settings, parent)

        ctx.add_settings_section(section)
