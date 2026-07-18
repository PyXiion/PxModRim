from __future__ import annotations

import asyncio
import time
from collections.abc import Callable
from importlib.resources import files as resource_files
from typing import TYPE_CHECKING

from loguru import logger
from PySide6.QtCore import QEvent, QObject, Qt
from PySide6.QtGui import (
    QCloseEvent,
    QIcon,
    QKeyEvent,
    QKeySequence,
    QResizeEvent,
    QShortcut,
)
from PySide6.QtQml import QQmlEngine
from PySide6.QtWidgets import (
    QApplication,
    QMainWindow,
    QSplitter,
    QStackedWidget,
    QVBoxLayout,
    QWidget,
)
from qasync import asyncSlot

from pxmodrim.core.config import LaunchStrategy, save_config
from pxmodrim.core.context import CoreContext
from pxmodrim.core.models.metadata.structures import AboutXmlMod
from pxmodrim.core.models.view.sidebar import SidebarEntry
from pxmodrim.ui.components import (
    HeaderController,
    HeaderPanel,
    SvgIconProvider,
    ToastManager,
    ViewRailPanel,
)
from pxmodrim.ui.components.dialogs import await_dialog
from pxmodrim.ui.panels.about_panel import AboutPanel
from pxmodrim.ui.panels.settings_panel import SettingsPanel
from pxmodrim.ui.theme.qml_theme import Theme
from pxmodrim.ui.views import builtin_views
from pxmodrim.ui.window.menu_bar import MenuBar

if TYPE_CHECKING:
    from pxmodrim.core.models.view.diagnostics import ModDiagnosticsView


class MainWindow(QMainWindow):
    def __init__(self, ctx: CoreContext) -> None:
        super().__init__()

        self._app_quit_callback: Callable[[], None] | None = None
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
        self._selected_uuid: str | None = None

        # Wire diagnostics service signals
        ctx.diagnostics_service.diagnostics_summary_changed.connect(
            self._on_diagnostics_summary_changed
        )

        # ── Frameless window attempt ──
        self._is_frameless = self._try_frameless()

        # QML quirks
        self.setFocusPolicy(Qt.FocusPolicy.ClickFocus)

        # ── Header (QML) ──
        self._header_controller = HeaderController(
            is_frameless=self._is_frameless,
            initial_strategy=int(self._ctx.config.ui.launch_strategy),
        )
        self._header_controller.refresh_requested.connect(self._refresh_mods)
        self._header_controller.sort_requested.connect(self._auto_sort)
        self._header_controller.save_requested.connect(self._save_mods_config)
        self._header_controller.settings_requested.connect(self._open_settings)
        self._header_controller.launch_requested.connect(self._launch_game)
        self._header_controller.strategy_changed.connect(self._on_strategy_changed)
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

        # ── Content: vertical icon rail + stacked views ──
        rail_tabs = [
            {"viewId": v.view_id, "icon": v.icon_name, "label": v.label}
            for v in builtin_views()
        ]
        self._rail = ViewRailPanel(rail_tabs, self._qml_engine)
        self._rail.setObjectName("viewRail")
        self._rail.setMinimumWidth(52)
        self._rail.setMaximumWidth(180)
        self._stack = QStackedWidget()
        self._stack.setObjectName("viewStack")

        self._splitter = QSplitter(Qt.Orientation.Horizontal)
        self._splitter.setContentsMargins(0, 0, 0, 0)
        self._splitter.setHandleWidth(1)
        self._splitter.addWidget(self._rail)
        self._splitter.addWidget(self._stack)
        self._splitter.setStretchFactor(0, 0)
        self._splitter.setStretchFactor(1, 1)
        self._splitter.setCollapsible(0, False)
        self._splitter.setCollapsible(1, False)
        self._splitter.setSizes([180, self.width() - 180])
        self._rail.currentChanged.connect(self._on_rail_tab_changed)
        self._rail.hovered.connect(self._on_rail_hovered)
        self._splitter.splitterMoved.connect(self._snap_rail)

        self._views: list = []
        for view_cls in builtin_views():
            view = view_cls(self._ctx, self._qml_engine)
            self._views.append(view)
            self._stack.addWidget(view)

        self._mods_view = next(
            (v for v in self._views if v.view_id == "mods"), None
        )
        if self._mods_view is not None:
            self.sidebar = self._mods_view.sidebar
            self.mod_list = self._mods_view.mod_list
            self.mod_info = self._mods_view.mod_info
            self._mods_view.entry_selected.connect(self._on_entry_selected)
            self._mods_view.mod_selected.connect(self._on_mod_selected)
            self._mods_view.active_mods_changed.connect(
                self._on_active_mods_changed, Qt.ConnectionType.QueuedConnection
            )
            self._mods_view.order_changed.connect(
                self._on_order_changed, Qt.ConnectionType.QueuedConnection
            )

        # Keep Steam Workshop badges in sync with the app's installed-mod set:
        # when mods change in the app, re-badge the open workshop page live.
        steam_view = next(
            (v for v in self._views if v.view_id == "steam_workshop"), None
        )
        if steam_view is not None and self._mods_view is not None:
            self._mods_view.active_mods_changed.connect(steam_view.refresh_badges)

        outer_layout.addWidget(self._splitter, stretch=1)
        self.setCentralWidget(outer)

        # ── Toast overlay (replaces status bar) ──
        self._toast_manager = ToastManager(self.centralWidget())
        self._toast_manager.resize_to_parent()

        # Wire deferred signal connections (widgets now exist)
        ctx.diagnostics_service.status_message_changed.connect(
            self._on_status_message
        )

        # Alt key event filter. Installed on the window itself, NOT on the
        # whole QApplication: an app-wide filter makes PySide wrap every
        # QObject that receives an event (including WebEngine's internal
        # widgets), and a dangling wrapper there SIGSEGVs in
        # getWrapperForQObject. The filter only ever acts on objects whose
        # window() is this one, so scoping to self is equivalent.
        self.installEventFilter(self)

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

    def set_app_quit_callback(self, callback: Callable[[], None]) -> None:
        """Store the callback used to end the async run loop on close."""
        self._app_quit_callback = callback

    def closeEvent(self, event: QCloseEvent) -> None:
        # Release WebEngine views (their dedicated profiles) before the
        # Qt widget tree is torn down. Then disconnect aboutToQuit: on this
        # Chromium/Qt build, WebEngine registers an aboutToQuit handler
        # that dereferences a freed object and SIGSEGVs during shutdown
        # once the Steam tab has been opened. We drop every aboutToQuit
        # connection (removing Chromium's) and instead fire our own quit
        # callback directly, so the async run loop ends cleanly and the
        # process exits without the crashing dispatch.
        for view in self._views:
            teardown = getattr(view, "teardown", None)
            if callable(teardown):
                teardown()

        app = QApplication.instance()
        if app is not None:
            app.aboutToQuit.disconnect()
        if self._app_quit_callback is not None:
            self._app_quit_callback()
        event.accept()
        self.deleteLater()

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
    async def _refresh_mods(self) -> None:
        self._toast_manager.info("Refreshing mods...")
        t0 = time.monotonic()
        await self._ctx.mod_service.reload()
        elapsed = time.monotonic() - t0
        if not self._ctx.all_mods:
            self._toast_manager.warning("No mods found")
            return
        self._toast_manager.info(
            f"Reloaded {len(self._ctx.all_mods)} mods in {elapsed:.1f}s", 3000
        )

    # ── Private slots ───────────────────────────────────────

    def _on_status_message(self, message: str) -> None:
        self._toast_manager.info(message)

    @asyncSlot()
    async def _open_settings(self) -> None:
        result, dialog = await await_dialog(SettingsPanel, self._ctx)
        if result != 1:
            return
        cfg = dialog.get_config()
        if not cfg.paths.game:
            logger.warning("Settings saved without a game path")
        save_config(cfg)
        self._ctx.update_config(cfg)
        from pxmodrim.core.providers import create_providers

        self._ctx.mod_service.reset_providers(
            create_providers(cfg.paths, self._ctx._pool)
        )
        await self._ctx.mod_service.reload()

    @asyncSlot()
    async def _show_about(self) -> None:
        await await_dialog(AboutPanel, self)

    @asyncSlot()
    async def _auto_sort(self) -> None:
        deps_to_enable = self._ctx.sort_service.resolve_missing_dependencies(
            set(self.mod_list.active_uuids())
        )
        if deps_to_enable:
            self.mod_list.enableMods(deps_to_enable)
            self._ctx.diagnostics_service.rebuild(self.mod_list.active_uuids())

        t0 = time.monotonic()
        ordered_uuids = await self._ctx.sort_service.sort_active_mods()
        elapsed = time.monotonic() - t0
        self.mod_list.model.reorder(ordered_uuids)
        self._ctx.diagnostics_service.reorder(ordered_uuids)
        self._toast_manager.success(
            f"Sorted {len(ordered_uuids)} mods in {elapsed:.1f}s", 5000
        )

    @asyncSlot()
    async def _save_mods_config(self) -> None:
        active_ids = self.mod_list.active_uuids()
        ok = await self._ctx.mod_service.save_active_layout(active_ids)
        if ok:
            self._toast_manager.success(f"Saved {len(active_ids)} active mods", 3000)
        else:
            self._toast_manager.warning("Config folder not set", 3000)

    @asyncSlot()
    async def _launch_game(self) -> None:
        logger.info("Launch requested")

        ok = await self._ctx.mod_service.save_active_layout(
            self.mod_list.active_uuids()
        )
        if not ok:
            logger.warning("Config folder not set — mod list not saved before launch")
            self._toast_manager.warning(
                "Config folder not set — mod list won't be saved"
            )

        success, msg = await self._ctx.game_launcher.launch()
        if success:
            logger.info(msg)
            self._toast_manager.success(msg)
        else:
            logger.warning(msg)
            self._toast_manager.warning(msg, 5000)

    def _on_strategy_changed(self, index: int) -> None:
        new_strategy = LaunchStrategy(index)
        if self._ctx.config.ui.launch_strategy != new_strategy:
            logger.info("Launch strategy changed to {}", new_strategy.name)
            self._ctx.config.ui.launch_strategy = new_strategy
            save_config(self._ctx.config)

    @asyncSlot()
    async def _on_mod_selected(self, uuid: str) -> None:
        self._selected_uuid = uuid
        item = self.mod_list.model.get_item_by_uuid(uuid)
        if item and item.mod:
            self.mod_info.show_mod(item.mod)
            self.mod_info.set_issues(self._ctx.diagnostics_service.issues_for(uuid))
            pid = getattr(item.mod, "package_id", None)
            await self.mod_info.set_time_analytics(
                str(pid) if pid else None,
                self._resolve_active_pids(),
            )

    @asyncSlot()
    async def _on_active_mods_changed(self) -> None:
        if self.mod_list.model.rowCount() == 0:
            return
        uuids = self.mod_list.active_uuids()
        self._ctx.diagnostics_service.rebuild(uuids)
        await asyncio.sleep(0)
        self._apply_current_sidebar_filter()
        if self._selected_uuid:
            await self._on_mod_selected(self._selected_uuid)

    def _resolve_active_pids(self) -> list[str]:
        pids: list[str] = []
        for uuid in self.mod_list.active_uuids():
            mod = self._ctx.all_mods.get(uuid)
            if isinstance(mod, AboutXmlMod):
                pids.append(str(mod.package_id).lower())
        return pids

    def _on_order_changed(self) -> None:
        uuids = self.mod_list.active_uuids()
        self._ctx.diagnostics_service.reorder(
            uuids if uuids else self._ctx.active_uuids
        )

    def _apply_current_sidebar_filter(self) -> None:
        if self._mods_view is not None:
            self._mods_view.apply_current_sidebar_filter()

    def _apply_entry(self, entry: SidebarEntry) -> None:
        if self._mods_view is not None:
            self._mods_view.apply_sidebar_entry(entry)

    def _on_entry_selected(self, entry: SidebarEntry) -> None:
        self._apply_entry(entry)

    def _on_rail_tab_changed(self, index: int) -> None:
        self._stack.setCurrentIndex(index)
        # Preload an adjacent tab (e.g. the Steam view next to Mods) so its
        # content is already warm when the user moves to it.
        for adjacent in (index - 1, index + 1):
            if 0 <= adjacent < len(self._views):
                preload = getattr(self._views[adjacent], "preload", None)
                if callable(preload):
                    preload()

    def _on_rail_hovered(self, index: int) -> None:
        # Preload view content (e.g. spin up Chromium for the Steam tab)
        # when the user hovers its rail entry, so the first click is fast
        # without paying the startup cost up front.
        if 0 <= index < len(self._views):
            preload = getattr(self._views[index], "preload", None)
            if callable(preload):
                preload()

    def _snap_rail(self) -> None:
        target = 180 if self._rail.width() >= 116 else 52
        total = self.width()
        self._splitter.setSizes([target, total - target])

    # ── Diagnostics summary callback ─────────────────────────

    def _on_diagnostics_summary_changed(
        self, diagnostics: dict[str, ModDiagnosticsView]
    ) -> None:
        if self._selected_uuid is not None:
            self.mod_info.set_issues(
                self._ctx.diagnostics_service.issues_for(self._selected_uuid)
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
