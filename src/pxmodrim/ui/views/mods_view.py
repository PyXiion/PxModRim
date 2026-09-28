from __future__ import annotations

from typing import TYPE_CHECKING

from PySide6.QtCore import Qt, Signal

if TYPE_CHECKING:
    from pxmodrim.ui.context import AppContext
from PySide6.QtQml import QQmlEngine
from PySide6.QtWidgets import QHBoxLayout, QWidget

from pxmodrim.core.context import CoreContext
from pxmodrim.ui.panels.mod_info_panel import ModInfoPanel
from pxmodrim.ui.panels.mod_list_panel import ModListPanel
from pxmodrim.ui.panels.sidebar_panel import SidebarPanel
from pxmodrim.ui.views.base import BaseViewPanel


class ModsViewPanel(BaseViewPanel):
    view_id = "mods"
    icon_name = "mods"
    label = "Mods"

    entry_selected = Signal(object)
    mod_selected = Signal(str)
    active_mods_changed = Signal()
    order_changed = Signal()

    def __init__(
        self,
        ctx: CoreContext,
        qml_engine: QQmlEngine | None = None,
        parent: QWidget | None = None,
        app_ctx: AppContext | None = None,
    ) -> None:
        """Initialize mods view with sidebar, mod list, and mod info panels."""
        super().__init__(ctx, qml_engine, parent, app_ctx=app_ctx)

        content = QWidget(self)
        content.setObjectName("contentArea")
        h_layout = QHBoxLayout(content)
        h_layout.setContentsMargins(0, 0, 0, 0)
        h_layout.setSpacing(0)

        self.sidebar = SidebarPanel(self._ctx, self._qml_engine, content)
        self.sidebar.setObjectName("sidebarPanel")
        self.sidebar.setFixedWidth(240)
        self.sidebar.entry_selected.connect(self.entry_selected)
        h_layout.addWidget(self.sidebar)

        self.mod_list = ModListPanel(self._ctx, self._qml_engine, content)
        self.mod_list.setObjectName("modListPanel")
        self.mod_list.mod_selected.connect(self.mod_selected)
        self.mod_list.active_mods_changed.connect(
            self.active_mods_changed, Qt.ConnectionType.QueuedConnection
        )
        self.mod_list.order_changed.connect(
            self.order_changed, Qt.ConnectionType.QueuedConnection
        )
        h_layout.addWidget(self.mod_list, stretch=3)

        self.mod_info = ModInfoPanel(
            self._ctx, self._qml_engine, ui_prefs=self._ui_prefs, parent=content
        )
        self.mod_info.setObjectName("modInfoPanel")
        self.mod_info.setMinimumWidth(300)
        h_layout.addWidget(self.mod_info, stretch=2)

        self._root.addWidget(content, stretch=1)

    def apply_sidebar_entry(self, entry: object) -> None:
        self.mod_list.clearSelection()
        from pxmodrim.core.models.view.sidebar import SidebarEntry

        if isinstance(entry, SidebarEntry):
            self.mod_list.set_sidebar_filter(entry.visible_uuids)

    def apply_current_sidebar_filter(self) -> None:
        entry = self.sidebar.current_entry()
        if entry is not None:
            self.mod_list.set_sidebar_filter(entry.visible_uuids)
