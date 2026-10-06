from __future__ import annotations

import asyncio
from collections.abc import Callable
from importlib.resources import files as resource_files
from typing import TYPE_CHECKING

import msgspec
from loguru import logger
from PySide6.QtCore import QEvent, QObject, Qt, QUrl
from PySide6.QtGui import (
    QAction,
    QCloseEvent,
    QDesktopServices,
    QIcon,
    QKeyEvent,
    QKeySequence,
    QResizeEvent,
)
from PySide6.QtQml import QQmlEngine
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QDialog,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QSplitter,
    QStackedWidget,
    QVBoxLayout,
    QWidget,
)
from qasync import asyncSlot

from pxmodrim.core.config import config_dir
from pxmodrim.core.constants import AfterLaunch, LaunchStrategy
from pxmodrim.core.downloads import DownloadProgress, DownloadResult, download_manager
from pxmodrim.core.models.view.sidebar import SidebarEntry
from pxmodrim.core.services.update_service import UpdateCheckError, UpdateService
from pxmodrim.core.support import get_app_version
from pxmodrim.ui.components import (
    HeaderController,
    HeaderPanel,
    ToastManager,
    ViewRailPanel,
    create_qml_engine,
)
from pxmodrim.ui.components.dialogs import await_dialog
from pxmodrim.ui.components.mod_updates import (
    confirm_update,
)
from pxmodrim.ui.config import save_ui_prefs
from pxmodrim.ui.context import AppContext
from pxmodrim.ui.mod_selection import ModSelectionPresenter
from pxmodrim.ui.navigation import SETTINGS_VIEW_ID, build_route, parse_route
from pxmodrim.ui.panels.about_panel import AboutPanel
from pxmodrim.ui.panels.keyboard_shortcuts_dialog import (
    QML_SHORTCUTS,
    KeyboardShortcutsDialog,
)
from pxmodrim.ui.panels.restore_snapshot_dialog import (
    ConfirmRestoreDialog,
    RestoreSnapshotDialog,
)
from pxmodrim.ui.panels.settings_panel import SettingsPanel
from pxmodrim.ui.panels.update_dialog import SKIP_RESULT, UpdateDialog
from pxmodrim.ui.panels.upload_report_dialog import handle_upload_report
from pxmodrim.ui.theme.constants import (
    RAIL_COLLAPSE_WIDTH,
    RAIL_MAX_WIDTH,
    RAIL_MIN_WIDTH,
)
from pxmodrim.ui.theme.qml_theme import Theme
from pxmodrim.ui.window.actions import (
    ACTIONS,
    VIEW_SWITCH_KEYS,
    ActionId,
    create_actions,
    shortcut_rows,
)
from pxmodrim.ui.window.menu_bar import MenuBar

_ISSUES_URL = "https://github.com/PyXiion/PxModRim/issues"
_HELP_ACTIONS = (
    ActionId.REPORT_ISSUE,
    ActionId.UPLOAD_LOGS,
    ActionId.OPEN_LOGS,
    ActionId.SHORTCUTS,
    ActionId.CHECK_UPDATES,
    ActionId.ABOUT,
)
_WINDOW_TITLE = "PxModRim[*]"


if TYPE_CHECKING:
    from pxmodrim.core.models.view.diagnostics import ModDiagnosticsView


class UnsavedChangesDialog(QMessageBox):
    def __init__(self, parent: QWidget) -> None:
        super().__init__(parent)
        self.setIcon(QMessageBox.Icon.Question)
        self.setWindowTitle("Unsaved Changes")
        self.setText("The active mod list has unsaved changes.")
        self.setInformativeText(
            "Save before closing, discard the changes, or cancel closing."
        )
        self.setStandardButtons(
            QMessageBox.StandardButton.Save
            | QMessageBox.StandardButton.Discard
            | QMessageBox.StandardButton.Cancel
        )
        self.setDefaultButton(QMessageBox.StandardButton.Save)


class LaunchConfirmDialog(QMessageBox):
    def __init__(self, parent: QWidget, title: str, text: str, info: str, accept: str):
        super().__init__(parent)
        self.setIcon(QMessageBox.Icon.Warning)
        self.setWindowTitle(title)
        self.setText(text)
        self.setInformativeText(info)
        self.setStandardButtons(
            QMessageBox.StandardButton.Ok | QMessageBox.StandardButton.Cancel
        )
        self.button(QMessageBox.StandardButton.Ok).setText(accept)
        self.setDefaultButton(QMessageBox.StandardButton.Cancel)
        self.setCheckBox(QCheckBox("Don't ask again", self))

    def dont_ask_again(self) -> bool:
        box = self.checkBox()
        return box is not None and box.isChecked()


class MainWindow(QMainWindow):
    def __init__(self, app_ctx: AppContext) -> None:
        """Initialize the main application window."""
        super().__init__()

        self._app_quit_callback: Callable[[], None] | None = None
        self._is_frameless: bool = False
        self._app_ctx = app_ctx
        self._ctx = app_ctx.core
        self._ui_prefs = app_ctx.ui_prefs
        self._selected_uuid: str | None = None
        self._mods_view = None
        self._selection: ModSelectionPresenter | None = None
        self._saved_active_uuids = self._ctx.loaded_active_uuids
        self._unsaved_changes = False
        self._close_prompt_open = False
        self._close_confirmed = False
        self._close_task: asyncio.Task[None] | None = None
        self._update_service = UpdateService(get_app_version())
        self._update_task: asyncio.Task[None] | None = None
        self._launch_task: asyncio.Task[None] | None = None

        self._setup_window_basics()
        self._setup_qml()
        self._setup_header_and_shortcuts()
        self._ctx.active_state_changed.connect(self._on_active_state_changed)
        self._ctx.mod_service.mods_changed.connect(self._on_mods_reloaded)
        self._setup_content_and_views()
        self._setup_toast_and_events()

    def _setup_window_basics(self) -> None:
        logger.debug("main_window: setting up window basics")
        self.setWindowTitle(_WINDOW_TITLE)
        self.setWindowIcon(
            QIcon(str(resource_files("pxmodrim.ui.assets") / "logo.svg"))
        )
        self.resize(1400, 850)

        self._ctx.diagnostics_service.diagnostics_summary_changed.connect(
            self._on_diagnostics_summary_changed
        )

        self._is_frameless = self._try_frameless()
        self.setFocusPolicy(Qt.FocusPolicy.ClickFocus)

    def _setup_qml(self) -> None:
        logger.debug("main_window: setting up QML engine and theme")
        self._qml_engine = create_qml_engine(self)
        self._theme = Theme(self)
        self._qml_engine.rootContext().setContextProperty("Theme", self._theme)

    @property
    def qml_engine(self) -> QQmlEngine:
        return self._qml_engine

    def _setup_header_and_shortcuts(self) -> None:
        logger.debug("main_window: setting up header and shortcuts")
        self._downloads = download_manager(self._ctx)
        self._downloads_refresh: asyncio.Task[int] | None = None
        self._header_controller = HeaderController(
            is_frameless=self._is_frameless,
            initial_strategy=int(self._ui_prefs.launch_strategy),
            app_version=get_app_version(),
            tooltips={
                "refresh": ACTIONS[ActionId.REFRESH].tooltip(),
                "sort": ACTIONS[ActionId.AUTO_SORT].tooltip(),
                "save": ACTIONS[ActionId.SAVE].tooltip(),
                "update_mods": ACTIONS[ActionId.UPDATE_MODS].tooltip(),
            },
            downloads_available=self._downloads.available,
        )
        self._header_controller.refresh_requested.connect(self._refresh_mods)
        self._header_controller.sort_requested.connect(self._auto_sort)
        self._header_controller.save_requested.connect(self._save_mods_config)
        self._header_controller.launch_requested.connect(self._launch_game)
        self._header_controller.strategy_changed.connect(self._on_strategy_changed)
        self._header_controller.minimize_requested.connect(self.showMinimized)
        self._header_controller.maximize_requested.connect(self._toggle_maximized)
        self._header_controller.close_requested.connect(self.close)
        self._header_controller.drag_started.connect(self._start_system_move)
        self._header_controller.downloads_requested.connect(
            lambda: self._app_ctx.navigate(build_route("downloads"))
        )
        self._app_ctx.set_navigator(self._open_route)
        self._header_controller.update_mods_requested.connect(self._update_mods)

        self._header = HeaderPanel(self._header_controller, self._qml_engine)

        self._actions = create_actions(self)
        handlers = {
            ActionId.SAVE: self._save_mods_config,
            ActionId.RESTORE: self._restore_snapshot,
            ActionId.SETTINGS: lambda: self._app_ctx.navigate(
                build_route(SETTINGS_VIEW_ID)
            ),
            ActionId.QUIT: self.close,
            ActionId.REFRESH: self._refresh_mods,
            ActionId.FULL_RESCAN: self._full_rescan,
            ActionId.UPDATE_MODS: self._update_mods,
            ActionId.AUTO_SORT: self._auto_sort,
            ActionId.FOCUS_SEARCH: self._focus_search,
            ActionId.NEXT_VIEW: lambda: self._cycle_view(1),
            ActionId.PREV_VIEW: lambda: self._cycle_view(-1),
            ActionId.FULLSCREEN: self._toggle_fullscreen,
            ActionId.REPORT_ISSUE: self._open_issue_tracker,
            ActionId.UPLOAD_LOGS: self._upload_log_and_system_info,
            ActionId.OPEN_LOGS: self._open_logs_folder,
            ActionId.SHORTCUTS: self._show_shortcuts,
            ActionId.CHECK_UPDATES: self._check_updates_manually,
            ActionId.ABOUT: self._show_about,
        }
        for action_id, handler in handlers.items():
            self._actions[action_id].triggered.connect(handler)
        self._actions[ActionId.UPDATE_MODS].setEnabled(self._downloads.available)

        for index, key in enumerate(VIEW_SWITCH_KEYS):
            switch = QAction(self)
            switch.setShortcut(QKeySequence(key))
            switch.triggered.connect(lambda _=False, i=index: self._select_view(i))
            self.addAction(switch)

        self._menu_bar = MenuBar(self._actions, self)

    def _focus_search(self) -> None:
        search = getattr(self._stack.currentWidget(), "search_input", None)
        if isinstance(search, QLineEdit):
            search.setFocus()
        elif self._mods_view is not None:
            self._mods_view.mod_list.search_input.setFocus()

    def _toggle_fullscreen(self) -> None:
        if self.isFullScreen():
            self.showNormal()
        else:
            self.showFullScreen()

    def _open_route(self, url: str) -> None:
        route = parse_route(url)
        if route is None:
            return
        if route.view_id == SETTINGS_VIEW_ID:
            self._open_settings()  # pyright: ignore[reportUnusedCoroutine]
            return
        for index, view in enumerate(self._views):
            if view.view_id == route.view_id:
                self._show_view(index)
                view.open_route(route.path)
                return

    def _select_view(self, index: int) -> None:
        if 0 <= index < self._stack.count():
            self._app_ctx.navigate(build_route(self._views[index].view_id))

    def _show_view(self, index: int) -> None:
        self._rail.set_current(index)
        self._on_rail_tab_changed(index)

    def _cycle_view(self, direction: int) -> None:
        count = self._stack.count()
        if count:
            self._select_view((self._stack.currentIndex() + direction) % count)

    @asyncSlot()
    async def _full_rescan(self) -> None:
        await self._reload_mods(full=True)

    @asyncSlot()
    async def _show_shortcuts(self) -> None:
        await await_dialog(
            KeyboardShortcutsDialog, shortcut_rows() + QML_SHORTCUTS, self
        )

    @staticmethod
    def _open_issue_tracker() -> None:
        QDesktopServices.openUrl(QUrl(_ISSUES_URL))

    @staticmethod
    def _open_logs_folder() -> None:
        QDesktopServices.openUrl(QUrl.fromLocalFile(str(config_dir() / "logs")))

    def _on_downloads_progress(self, progress: DownloadProgress) -> None:
        self._header_controller.set_downloads_progress(
            f"Updating mods: {progress.completed} / {progress.total}"
            f" · {progress.bytes_done / 2**20:.0f} MB",
            progress.completed,
            progress.total,
        )

    def _setup_content_and_views(self) -> None:
        logger.debug("main_window: setting up content and views")
        rail_views = self._app_ctx.rail_views

        outer = QWidget()
        outer.setObjectName("outerContainer")
        outer_layout = QVBoxLayout(outer)
        outer_layout.setContentsMargins(0, 0, 0, 0)
        outer_layout.setSpacing(0)
        outer_layout.addWidget(self._header)
        outer_layout.addWidget(self._menu_bar)
        self._menu_bar.hide()

        rail_tabs = [
            {"viewId": v.view_id, "icon": v.icon_name, "label": v.label}
            for v in rail_views
        ]
        self._rail = ViewRailPanel(
            rail_tabs,
            [{"id": str(aid), "label": ACTIONS[aid].label} for aid in _HELP_ACTIONS],
            self._qml_engine,
        )
        self._rail.setObjectName("viewRail")
        self._rail.setMinimumWidth(RAIL_MIN_WIDTH)
        self._rail.setMaximumWidth(RAIL_MAX_WIDTH)
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
        rail_width = RAIL_MIN_WIDTH if self._ui_prefs.rail_collapsed else RAIL_MAX_WIDTH
        self._splitter.setSizes([rail_width, self.width() - rail_width])
        self._rail.currentChanged.connect(self._select_view)
        self._rail.hovered.connect(self._on_rail_hovered)
        self._rail.settings_requested.connect(
            lambda: self._app_ctx.navigate(build_route(SETTINGS_VIEW_ID))
        )
        self._rail.help_action_requested.connect(
            lambda action_id: self._actions[ActionId(action_id)].trigger()
        )
        self._splitter.splitterMoved.connect(self._snap_rail)

        self._views: list = []
        for view_cls in rail_views:
            view = view_cls(
                self._ctx, self._qml_engine, parent=self._stack, app_ctx=self._app_ctx
            )
            self._views.append(view)
            self._stack.addWidget(view)

        self._mods_view = next((v for v in self._views if v.view_id == "mods"), None)
        if self._mods_view is not None:
            self.sidebar = self._mods_view.sidebar
            self.mod_list = self._mods_view.mod_list
            self.mod_info = self._mods_view.mod_info
            self._mods_view.entry_selected.connect(self._on_entry_selected)
            self._mods_view.mod_selected.connect(self._on_mod_selected)
            self.mod_info.mod_requested.connect(self._on_mod_requested)
            self._selection = ModSelectionPresenter(self._ctx, self.mod_info)

        outer_layout.addWidget(self._splitter, stretch=1)
        self.setCentralWidget(outer)

    def _setup_toast_and_events(self) -> None:
        logger.debug("main_window: setting up toast manager and events")
        self._toast_manager = ToastManager(self.centralWidget())
        self._toast_manager.resize_to_parent()
        self._app_ctx.toasts = self._toast_manager

        self._ctx.diagnostics_service.status_message_changed.connect(
            self._on_status_message
        )
        self._ctx.diagnostics_service.background_task_changed.connect(
            self._header_controller.set_task_text
        )
        if self._downloads.available:
            self._downloads.busy_changed.connect(self._on_downloads_busy)
            self._downloads.download_progress.connect(self._on_downloads_progress)
            self._downloads.download_finished.connect(self._on_downloads_finished)
            self._downloads.status_message_changed.connect(self._on_status_message)

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
        except (OSError, AttributeError) as exc:
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

    async def _confirm_close(self) -> None:
        try:
            result, _ = await await_dialog(UnsavedChangesDialog, self)
        finally:
            self._close_prompt_open = False

        if result == QMessageBox.StandardButton.Save:
            if not await self._save_active_mods():
                return
        elif result != QMessageBox.StandardButton.Discard:
            return

        self._close_confirmed = True
        self.close()

    def closeEvent(self, event: QCloseEvent) -> None:
        if self._unsaved_changes and not self._close_confirmed:
            event.ignore()
            if not self._close_prompt_open:
                self._close_prompt_open = True
                self._close_task = asyncio.create_task(self._confirm_close())
            return
        logger.info("main_window: shutting down")
        if self._update_task is not None:
            self._update_task.cancel()
        if self._launch_task is not None:
            self._launch_task.cancel()
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
        await self._reload_mods(full=False)

    async def _reload_mods(self, *, full: bool) -> None:
        self._toast_manager.info(
            "Rescanning all mods\u2026" if full else "Refreshing mods\u2026"
        )
        count = await self._app_ctx.refresh_mods(full=full)
        if not count:
            self._toast_manager.warning("No mods found")
            return
        self._toast_manager.success(
            f"Rescanned {count} mods" if full else f"Reloaded {count} mods"
        )

    # ── Private slots ───────────────────────────────────────

    def _on_status_message(self, message: str) -> None:
        self._toast_manager.info(message)

    @asyncSlot()
    async def _update_mods(self) -> None:
        downloads = self._downloads
        if not downloads.available:
            return
        if downloads.is_downloading:
            logger.info("[downloads] stop requested from the header")
            downloads.cancel()
            return
        ids = downloads.updatable_ids(self._ctx.all_mods.values())
        if not ids:
            self._toast_manager.info("No downloaded mods to update")
            return
        if not await confirm_update(len(ids), self):
            return
        logger.info("[downloads] update-all requested for {} mods", len(ids))
        self._toast_manager.info(f"Updating {len(ids)} mods\u2026")
        try:
            await downloads.download_mods(ids)
        except (RuntimeError, ValueError) as exc:
            self._toast_manager.error(str(exc))

    def _on_downloads_busy(self, busy: bool) -> None:
        self._header_controller.set_downloads_busy(busy)
        self._actions[ActionId.UPDATE_MODS].setEnabled(not busy)
        if busy:
            self._header_controller.set_downloads_progress("Updating mods…", 0, 0)

    def _on_downloads_finished(self, result: DownloadResult) -> None:
        ok, failed = len(result.succeeded), len(result.failed)
        changed = len(result.changed)
        if not ok and not failed:
            self._toast_manager.info("Download cancelled")
            return
        summary = f"{changed} updated, {ok - changed} unchanged"
        if failed:
            by_id = download_manager(self._ctx).names_by_id(self._ctx.all_mods.values())
            shown = ", ".join(by_id.get(pid, pid) for pid in result.failed[:3])
            more = "…" if failed > 3 else ""
            self._toast_manager.warning(f"{summary}, {failed} failed: {shown}{more}")
        else:
            self._toast_manager.success(summary)
        if changed:
            self._downloads_refresh = asyncio.ensure_future(
                self._app_ctx.refresh_mods()
            )

    @asyncSlot()
    async def _open_settings(self) -> None:
        result, dialog = await await_dialog(
            SettingsPanel, self._ctx, self._qml_engine, self
        )
        if result != QDialog.DialogCode.Accepted:
            return
        cfg = dialog.get_config()
        if not cfg.paths.game:
            logger.warning("Settings saved without a game path")
        self._ctx.config_service.save("config.json", cfg)
        self._ctx.update_config(cfg)
        self._ctx.reset_providers(cfg.paths)
        await self._ctx.mod_service.reload()
        self._toast_manager.success("Settings saved")

    @asyncSlot()
    async def _show_about(self) -> None:
        await await_dialog(
            AboutPanel, self, ctx=self._ctx, toast_manager=self._toast_manager
        )

    def start_startup_update_check(self) -> None:
        self._start_update_check(manual=False)

    def _check_updates_manually(self) -> None:
        self._start_update_check(manual=True)

    def _start_update_check(self, *, manual: bool) -> None:
        if self._update_task is not None and not self._update_task.done():
            return
        self._update_task = asyncio.create_task(self._check_for_updates(manual=manual))

    async def _check_for_updates(self, *, manual: bool) -> None:
        try:
            release = await self._update_service.check()
        except UpdateCheckError as exc:
            logger.warning("Update check failed: {}", exc)
            if manual:
                self._toast_manager.error("Could not check for updates")
            return
        if release is None:
            if manual:
                self._toast_manager.success("PxModRim is up to date")
            return
        if not manual and release.tag == self._ui_prefs.skipped_update_tag:
            logger.debug("Update {} was skipped by the user", release.tag)
            return

        result, _ = await await_dialog(
            UpdateDialog, release, self._update_service.current_version, self
        )
        if result == QDialog.DialogCode.Accepted:
            QDesktopServices.openUrl(QUrl(release.url))
        elif result == SKIP_RESULT:
            self._ui_prefs.skipped_update_tag = release.tag
            save_ui_prefs(self._ui_prefs, self._ctx.config_service)

    @asyncSlot()
    async def _upload_log_and_system_info(self) -> None:
        await handle_upload_report(self, self._ctx, self._toast_manager)

    @asyncSlot()
    async def _restore_snapshot(self) -> None:
        snapshots = self._ctx.mod_service.get_snapshots()
        if not snapshots:
            self._toast_manager.warning("No saved mod lists are available")
            return

        result, dialog = await await_dialog(RestoreSnapshotDialog, snapshots, self)
        if result != QDialog.DialogCode.Accepted:
            return

        chosen = dialog.selected_snapshot
        if chosen is None:
            return
        confirm_result, _ = await await_dialog(ConfirmRestoreDialog, chosen.name, self)
        if confirm_result != QMessageBox.StandardButton.Yes:
            return

        if await self._ctx.mod_service.restore_snapshot(chosen):
            self._toast_manager.success(f"Restored mod list from {chosen.name}")
        else:
            self._toast_manager.error("The saved mod list is invalid or cannot be read")

    @asyncSlot()
    async def _auto_sort(self) -> None:
        count, elapsed = await self._ctx.auto_sort()
        self._toast_manager.success(f"Sorted {count} mods in {elapsed:.0f}ms")

    def _on_active_state_changed(self, _active_uuids: tuple[str, ...]) -> None:
        self._refresh_unsaved_state()

    def _on_mods_reloaded(self, _: None = None) -> None:
        # mods_changed fires after the refresh finishes, so edits made since
        # ctx.load() are already in ctx.active_uuids; the baseline must be the
        # list that was read from disk.
        self._saved_active_uuids = self._ctx.loaded_active_uuids
        self._refresh_unsaved_state()

    def _refresh_unsaved_state(self) -> None:
        self._unsaved_changes = self._ctx.active_uuids != self._saved_active_uuids
        self._header_controller.set_unsaved_changes(self._unsaved_changes)
        self.setWindowModified(self._unsaved_changes)

    async def _write_active_layout(self) -> list[str] | None:
        """Persist the active list; return it on success, None if not saved."""
        active_ids = self.mod_list.active_uuids()
        if not await self._ctx.mod_service.save_active_layout(active_ids):
            return None
        self._saved_active_uuids = list(active_ids)
        self._refresh_unsaved_state()
        return active_ids

    @asyncSlot()
    async def _save_mods_config(self) -> None:
        await self._save_active_mods()

    async def _save_active_mods(self) -> bool:
        saved = await self._write_active_layout()
        if saved is None:
            self._toast_manager.warning("Config folder not set")
            return False
        self._toast_manager.success(f"Saved {len(saved)} active mods")
        return True

    @asyncSlot()
    async def _launch_game(self) -> None:
        if self._header_controller.launchState != "idle":
            return
        logger.info("Launch requested")
        self._header_controller.set_launch_state("launching")
        tracking = False
        try:
            tracking = await self._start_game()
        finally:
            if not tracking:
                self._header_controller.set_launch_state("idle")

    async def _confirm_launch(
        self, setting: str, title: str, text: str, info: str, accept: str
    ) -> bool:
        result, dialog = await await_dialog(
            LaunchConfirmDialog, self, title, text, info, accept
        )
        if result != QMessageBox.StandardButton.Ok:
            return False
        if dialog.dont_ask_again():
            cfg = msgspec.structs.replace(self._ctx.config, **{setting: False})
            self._ctx.config_service.save("config.json", cfg)
            self._ctx.update_config(cfg)
        return True

    async def _start_game(self) -> bool:
        launcher = self._ctx.game_launcher

        cfg = self._ctx.config
        running = cfg.launch_confirm_running and await asyncio.to_thread(
            launcher.is_running
        )
        errors = self._ctx.diagnostics_service.active_error_count()
        prompts = [
            (
                running,
                "launch_confirm_running",
                "Game Already Running",
                "RimWorld is already running.",
                "A second copy can overwrite the first one's saves and settings.",
                "Launch Anyway",
            ),
            (
                cfg.launch_confirm_errors and errors > 0,
                "launch_confirm_errors",
                "Mod List Has Errors",
                f"{errors} active mod(s) have errors.",
                "Missing dependencies or wrong load order can crash the game.",
                "Launch Anyway",
            ),
            (
                cfg.launch_confirm_unsaved and self._unsaved_changes,
                "launch_confirm_unsaved",
                "Unsaved Changes",
                "The active mod list has unsaved changes.",
                "The list is saved to ModsConfig.xml before the game starts.",
                "Save and Launch",
            ),
        ]
        for needed, setting, title, text, info, accept in prompts:
            if needed and not await self._confirm_launch(
                setting, title, text, info, accept
            ):
                return False

        if await self._write_active_layout() is None:
            logger.warning("Config folder not set — mod list not saved before launch")
            self._toast_manager.warning(
                "Config folder not set — mod list won't be saved"
            )

        success, msg = await launcher.launch(self._ui_prefs.launch_strategy)
        if not success:
            logger.warning(msg)
            self._toast_manager.warning(msg)
            return False

        logger.info(msg)
        self._toast_manager.success(msg)
        self._launch_task = asyncio.create_task(self._track_game())
        return True

    async def _track_game(self) -> None:
        launcher = self._ctx.game_launcher
        after = self._ctx.config.after_launch
        try:
            if not await launcher.wait_for_start():
                self._toast_manager.warning("Game did not start")
                return
            self._header_controller.set_launch_state("running")
            if after == AfterLaunch.CLOSE and not self._unsaved_changes:
                self._close_confirmed = True
                self.close()
                return
            if after == AfterLaunch.MINIMIZE:
                self.showMinimized()
            code = await launcher.wait_for_exit()
            if code:
                logger.warning("Game exited with code {}", code)
                self._toast_manager.warning(f"Game exited with code {code}")
            else:
                self._toast_manager.info("Game closed")
            if after == AfterLaunch.MINIMIZE:
                self.showNormal()
                self.activateWindow()
        finally:
            self._header_controller.set_launch_state("idle")

    def _on_strategy_changed(self, index: int) -> None:
        new_strategy = LaunchStrategy(index)
        if self._ui_prefs.launch_strategy != new_strategy:
            logger.info("Launch strategy changed to {}", new_strategy.name)
            self._ui_prefs.launch_strategy = new_strategy
            save_ui_prefs(self._ui_prefs, self._ctx.config_service)

    @asyncSlot(str)
    async def _on_mod_requested(self, uuid: str) -> None:
        if self.mod_list.select_uuid(uuid):
            return
        # Hidden by the search box or a sidebar filter: reset both, then retry.
        self.mod_list.search_input.clear()
        self.sidebar.select_all_entry()
        if not self.mod_list.select_uuid(uuid):
            await self._on_mod_selected(uuid)

    @asyncSlot()
    async def _on_mod_selected(self, uuid: str) -> None:
        self._selected_uuid = uuid or None
        if self._selection is None:
            return
        if uuid:
            await self._selection.show(uuid)
        else:
            self._selection.clear()

    def _apply_current_sidebar_filter(self) -> None:
        if self._mods_view is not None:
            self._mods_view.apply_current_sidebar_filter()

    def _apply_entry(self, entry: SidebarEntry) -> None:
        if self._mods_view is not None:
            self._mods_view.apply_sidebar_entry(entry)

    def _on_entry_selected(self, entry: SidebarEntry) -> None:
        logger.debug(
            "main_window: sidebar entry selected: {}",
            entry.label if entry else None,
        )
        self._apply_entry(entry)

    def _on_rail_tab_changed(self, index: int) -> None:
        logger.debug("main_window: rail tab changed to {}", index)
        self._stack.setCurrentIndex(index)
        self._header_controller.set_downloads_progress_shown(
            self._views[index].view_id != "downloads"
        )
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
        target = (
            RAIL_MAX_WIDTH
            if self._rail.width() >= RAIL_COLLAPSE_WIDTH
            else RAIL_MIN_WIDTH
        )
        self._splitter.setSizes([target, self._splitter.width() - target])
        collapsed = target == RAIL_MIN_WIDTH
        if self._ui_prefs.rail_collapsed != collapsed:
            self._ui_prefs.rail_collapsed = collapsed
            save_ui_prefs(self._ui_prefs, self._ctx.config_service)

    # ── Diagnostics summary callback ─────────────────────────

    def _on_diagnostics_summary_changed(
        self, diagnostics: dict[str, ModDiagnosticsView]
    ) -> None:
        self._apply_current_sidebar_filter()
        if self._mods_view is not None and self._selected_uuid is not None:
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
