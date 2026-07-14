from __future__ import annotations

import asyncio
from importlib.resources import files as resource_files
from typing import TYPE_CHECKING

from loguru import logger
from PySide6.QtCore import QEvent, QObject, Qt
from PySide6.QtGui import QIcon, QKeyEvent, QKeySequence, QResizeEvent, QShortcut
from PySide6.QtQml import QQmlEngine
from PySide6.QtWidgets import (
    QApplication,
    QHBoxLayout,
    QMainWindow,
    QVBoxLayout,
    QWidget,
)
from qasync import asyncSlot

from pxmodrim._compat.config import save_config
from pxmodrim._compat.dialogs import await_dialog
from pxmodrim.core.context import CoreContext
from pxmodrim.core.mod_service import ModService
from pxmodrim.models.view.sidebar import SidebarEntry
from pxmodrim.services.diagnostics_service import DiagnosticsService
from pxmodrim.services.sort_service import SortService
from pxmodrim.ui.about_panel import AboutPanel
from pxmodrim.ui.components import (
    HeaderController,
    HeaderPanel,
    SvgIconProvider,
    ToastManager,
)
from pxmodrim.ui.menu_bar import MenuBar
from pxmodrim.ui.mod_info_panel import ModInfoPanel
from pxmodrim.ui.mod_list_panel import ModListPanel
from pxmodrim.ui.settings_panel import SettingsPanel
from pxmodrim.ui.sidebar_panel import SidebarPanel
from pxmodrim.ui.theme import Theme

if TYPE_CHECKING:
    from pxmodrim.models.view.diagnostics import ModDiagnosticsView


class MainWindow(QMainWindow):
    def __init__(
        self,
        ctx: CoreContext,
        mod_service: ModService,
        diagnostics_service: DiagnosticsService,
        sort_service: SortService,
    ) -> None:
        super().__init__()

        self._is_frameless: bool = False

        self.setWindowTitle("PxModRim")
        self.setWindowIcon(
            QIcon(str(resource_files("pxmodrim.ui.assets") / "logo.svg"))
        )
        self.resize(1400, 850)

        # Register theme singleton + SVG icon provider on a shared QML engine
        self._qml_engine = QQmlEngine(self)
        self._theme = Theme(self)
        self._qml_engine.rootContext().setContextProperty("Theme", self._theme)
        self._qml_engine.addImageProvider("icons", SvgIconProvider())

        self._ctx = ctx
        self._mod_service = mod_service
        self._diagnostics_service = diagnostics_service
        self._sort_service = sort_service
        self._selected_uuid: str | None = None

        # Wire diagnostics service signals
        self._diagnostics_service.diagnostics_summary_changed.connect(
            self._on_diagnostics_summary_changed
        )

        # ── Frameless window attempt ──
        self._is_frameless = self._try_frameless()

        # ── Header (QML) ──
        self._header_controller = HeaderController(is_frameless=self._is_frameless)
        self._header_controller.refresh_requested.connect(self.load_mods_async)
        self._header_controller.sort_requested.connect(self._auto_sort)
        self._header_controller.save_requested.connect(self._save_mods_config)
        self._header_controller.settings_requested.connect(self._open_settings)
        self._header_controller.launch_requested.connect(self._launch_game)
        self._header_controller.minimize_requested.connect(self.showMinimized)
        self._header_controller.maximize_requested.connect(self._toggle_maximized)
        self._header_controller.close_requested.connect(self.close)
        self._header_controller.drag_started.connect(self._start_system_move)

        self._header = HeaderPanel(self._header_controller, self._qml_engine)

        # ── Menu bar (hidden by default, toggled via Alt) ──
        self._menu_bar = MenuBar()
        self._menu_bar.settings_requested.connect(self._open_settings)
        self._menu_bar.about_requested.connect(self._show_about)

        # ── Keyboard shortcuts ──
        QShortcut(QKeySequence("Ctrl+,"), self, self._open_settings)
        QShortcut(QKeySequence("Ctrl+Q"), self, self.close)
        QShortcut(QKeySequence("F5"), self, self._header_controller.refresh)
        QShortcut(QKeySequence("Ctrl+S"), self, self._header_controller.save)

        # ── Outer container ──
        outer = QWidget()
        outer.setObjectName("outerContainer")
        outer_layout = QVBoxLayout(outer)
        outer_layout.setContentsMargins(0, 0, 0, 0)
        outer_layout.setSpacing(0)

        outer_layout.addWidget(self._header)
        outer_layout.addWidget(self._menu_bar)
        self._menu_bar.hide()

        # Content (3-panel workspace)
        content = QWidget()
        content.setObjectName("contentArea")
        h_layout = QHBoxLayout(content)
        h_layout.setContentsMargins(0, 0, 0, 0)
        h_layout.setSpacing(0)

        self.sidebar = SidebarPanel(self._qml_engine)
        self.sidebar.setObjectName("sidebarPanel")
        self.sidebar.setFixedWidth(240)
        self.sidebar.entry_selected.connect(self._on_entry_selected)
        h_layout.addWidget(self.sidebar)

        self.mod_list = ModListPanel(
            self._mod_service.provider_colors, self._qml_engine
        )
        self.mod_list.setObjectName("modListPanel")
        self.mod_list.mod_selected.connect(self._on_mod_selected)
        self.mod_list.active_mods_changed.connect(
            self._on_active_mods_changed, Qt.ConnectionType.QueuedConnection
        )
        self.mod_list.order_changed.connect(
            self._on_order_changed, Qt.ConnectionType.QueuedConnection
        )
        h_layout.addWidget(self.mod_list, stretch=3)

        self.mod_info = ModInfoPanel(self._ctx.config)
        self.mod_info.setObjectName("modInfoPanel")
        self.mod_info.setMinimumWidth(300)
        h_layout.addWidget(self.mod_info, stretch=2)

        outer_layout.addWidget(content, stretch=1)
        self.setCentralWidget(outer)

        # ── Toast overlay (replaces status bar) ──
        self._toast_manager = ToastManager(self.centralWidget())
        self._toast_manager.resize_to_parent()

        # Wire deferred signal connections (widgets now exist)
        self._diagnostics_service.status_message_changed.connect(
            self._on_status_message
        )
        self._diagnostics_service.sidebar_entries_changed.connect(
            self.sidebar.set_entries
        )

        # Alt key event filter (installed after all widgets exist)
        app = QApplication.instance()
        if app is not None:
            app.installEventFilter(self)

    def _try_frameless(self) -> bool:
        try:
            self.setWindowFlags(
                Qt.WindowType.FramelessWindowHint
                | Qt.WindowType.Window
                | Qt.WindowType.WindowMinimizeButtonHint
                | Qt.WindowType.WindowMaximizeButtonHint
                | Qt.WindowType.WindowCloseButtonHint
            )
            if self.windowFlags() & Qt.WindowType.FramelessWindowHint:
                logger.debug("Frameless window enabled")
                return True
            logger.info("Frameless not supported, using native frame")
            return False
        except Exception as exc:
            logger.warning("Failed to set frameless window: {}", exc)
            return False

    def _toggle_maximized(self) -> None:
        if self.isMaximized():
            self.showNormal()
        else:
            self.showMaximized()
        self._header_controller.set_maximized(self.isMaximized())

    def _start_system_move(self) -> None:
        if not self._is_frameless:
            return
        win = self.windowHandle()
        if win is not None:
            win.startSystemMove()

    # ── Event filter (Alt → toggle menu bar) ─────────────

    def eventFilter(self, obj: QObject, event: QEvent) -> bool:
        if isinstance(event, QKeyEvent) and event.type() == QEvent.Type.KeyPress:
            if (
                event.key() == Qt.Key.Key_Alt
                and isinstance(obj, QWidget)
                and obj.window() is self
            ):
                self._toggle_menu_bar()
                return True
            if (
                event.key() == Qt.Key.Key_Escape
                and self._menu_bar.isVisible()
                and isinstance(obj, QWidget)
                and obj.window() is self
            ):
                self._menu_bar.hide()
                return True
        return super().eventFilter(obj, event)

    def _toggle_menu_bar(self) -> None:
        shown = not self._menu_bar.isVisible()
        self._menu_bar.setVisible(shown)
        if shown:
            self._menu_bar.setFocus()

    # ── Public ──────────────────────────────────────────────

    @asyncSlot()
    async def load_mods_async(self) -> None:
        self._toast_manager.info("Scanning mods...")
        await self._mod_service.discover()
        if not self._ctx.all_mods:
            self._toast_manager.warning("No mods found")
            return

        self.mod_list.model.update_provider_colors(self._mod_service.provider_colors)
        self.mod_list.load_mods(self._ctx.all_mods, self._ctx.active_uuids)

        await self._diagnostics_service.initialize()
        self._diagnostics_service.rebuild(self._ctx.active_uuids)

    # ── Private slots ───────────────────────────────────────

    def _on_status_message(self, message: str) -> None:
        self._toast_manager.info(message)

    @asyncSlot()
    async def _open_settings(self) -> None:
        result, dialog = await await_dialog(SettingsPanel, self._ctx.config)
        if result != 1:
            return
        cfg = dialog.get_config()
        if not cfg.paths.game:
            logger.warning("Settings saved without a game path")
        save_config(cfg)
        self._ctx.update_config(cfg)
        from pxmodrim.core.providers import create_providers

        self._mod_service.reset_providers(create_providers(cfg.paths))
        await self.load_mods_async()

    @asyncSlot()
    async def _show_about(self) -> None:
        await await_dialog(AboutPanel, self)

    @asyncSlot()
    async def _auto_sort(self) -> None:
        ordered_uuids = await self._sort_service.sort_active_mods()
        self.mod_list.model.reorder(ordered_uuids)
        self._diagnostics_service.reorder(ordered_uuids)
        self._toast_manager.success(f"Sorted {len(ordered_uuids)} mods", 5000)

    @asyncSlot()
    async def _save_mods_config(self) -> None:
        active_ids = self.mod_list.active_uuids()
        ok = await self._mod_service.save_active_layout(active_ids)
        if ok:
            self._toast_manager.success(f"Saved {len(active_ids)} active mods", 3000)
        else:
            self._toast_manager.warning("Config folder not set", 3000)

    def _launch_game(self) -> None:
        self._toast_manager.info("Launch not implemented yet")

    def _on_mod_selected(self, uuid: str) -> None:
        self._selected_uuid = uuid
        item = self.mod_list.model.get_item_by_uuid(uuid)
        if item and item.mod:
            self.mod_info.show_mod(item.mod)
            self.mod_info.set_issues(self._diagnostics_service.issues_for(uuid))

    @asyncSlot()
    async def _on_active_mods_changed(self) -> None:
        if self.mod_list.model.rowCount() == 0:
            return
        self._diagnostics_service.rebuild(self.mod_list.active_uuids())
        await asyncio.sleep(0)
        self._apply_current_sidebar_filter()

    def _on_order_changed(self) -> None:
        uuids = self.mod_list.active_uuids()
        self._diagnostics_service.reorder(uuids if uuids else self._ctx.active_uuids)

    def _apply_current_sidebar_filter(self) -> None:
        if not hasattr(self, "sidebar") or not self.sidebar:
            return
        root = self.sidebar._qml.rootObject()
        if root is None:
            return
        list_view = root.findChild(QObject, "listView")
        if list_view is None:
            return
        selected = list_view.property("currentIndex")
        if 0 <= selected < len(self.sidebar._entries):
            self._apply_entry(self.sidebar._entries[selected])

    def _apply_entry(self, entry: SidebarEntry) -> None:
        self.mod_list.clearSelection()
        self.mod_list.model.set_sidebar_filter(entry.visible_uuids)

    def _on_entry_selected(self, entry: SidebarEntry) -> None:
        self._apply_entry(entry)

    # ── Diagnostics summary callback ─────────────────────────

    def _on_diagnostics_summary_changed(
        self, diagnostics: dict[str, ModDiagnosticsView]
    ) -> None:
        self.mod_list.model.set_diagnostics(diagnostics)
        if self._selected_uuid is not None:
            self.mod_info.set_issues(
                self._diagnostics_service.issues_for(self._selected_uuid)
            )

    # ── Resize ───────────────────────────────────────────────

    def resizeEvent(self, event: QResizeEvent) -> None:
        super().resizeEvent(event)
        if hasattr(self, "_toast_manager"):
            self._toast_manager.resize_to_parent()

    def changeEvent(self, event: QEvent) -> None:
        super().changeEvent(event)
        if event.type() == QEvent.Type.WindowStateChange:
            self._header_controller.set_maximized(self.isMaximized())
