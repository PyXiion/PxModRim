from __future__ import annotations

import asyncio
import os
import sys
import traceback
from importlib.resources import files as resource_files
from types import TracebackType

from loguru import logger
from PySide6.QtGui import QColor, QIcon, QPalette
from PySide6.QtWebEngineQuick import QtWebEngineQuick
from PySide6.QtWidgets import QApplication, QMessageBox
from qasync import QEventLoop

# Qt6 defaults to PassThrough, which causes QtWebEngine to render at
# integer buffer-scale (e.g. 1×) while the compositor upscales fractionally
# (e.g. 1.25×, 1.5×) – results in pixelated content.
# Rounding down (< 0.75) avoids the blurred upscale at the cost of smaller UI;
# this matches the workaround used by Anki and qutebrowser for QTBUG-113574.
os.environ.setdefault("QT_SCALE_FACTOR_ROUNDING_POLICY", "RoundPreferFloor")
os.environ.setdefault("QT_ENABLE_HIGHDPI_SCALING", "1")

from pxmodrim.core.config import (
    AppConfig,
    config_file_path,
    detect_game_paths,
    load_config,
    save_config,
)
from pxmodrim.core.context import CoreContext
from pxmodrim.ui.components.dialogs import await_dialog
from pxmodrim.ui.config import load_ui_prefs
from pxmodrim.ui.panels.settings_panel import SettingsPanel
from pxmodrim.ui.plugins import SteamCmdUiPlugin
from pxmodrim.ui.theme.palette import PALETTE, get_stylesheet
from pxmodrim.ui.views.mods_view import ModsViewPanel
from pxmodrim.ui.window.main_window import MainWindow


def _exception_hook(
    exc_type: type[BaseException],
    exc_value: BaseException,
    exc_tb: TracebackType | None,
) -> None:
    """Global exception handler for unhandled exceptions."""
    if exc_type is KeyboardInterrupt:
        return

    error_msg = "".join(traceback.format_exception(exc_type, exc_value, exc_tb))
    logger.error("Unhandled exception:\n{}", error_msg)
    app = QApplication.instance()
    if app is not None:
        QMessageBox.critical(
            None,
            "PxModRim - Crash",
            f"An unexpected error occurred:\n\n"
            f"{exc_type.__name__}: {exc_value}\n\nDetails have been logged.",
        )


def _async_exception_handler(loop: asyncio.AbstractEventLoop, context: dict) -> None:
    """Handle unhandled exceptions in async tasks."""
    exc = context.get("exception")
    if exc:
        logger.error("Unhandled async exception: {}", exc, exc_info=exc)
    else:
        logger.error("Unhandled async error: {}", context.get("message"))


sys.excepthook = _exception_hook


class App:
    """Top-level application class wiring together Qt, services, and the main window."""

    __slots__ = ("qt_app", "_ctx", "_ui_prefs", "main_window")

    def __init__(self) -> None:
        QtWebEngineQuick.initialize()
        self.qt_app = QApplication(sys.argv)
        icon = QIcon(str(resource_files("pxmodrim.ui.assets") / "logo.svg"))
        self.qt_app.setWindowIcon(icon)
        self.qt_app.setApplicationName("PxModRim")
        self.qt_app.setOrganizationName("PxModRim")
        self.qt_app.setApplicationDisplayName("PxModRim")
        self.qt_app.setDesktopFileName("pxmodrim")

        self._apply_theme()

    def _apply_theme(self) -> None:
        """Set Fusion style, dark palette, stylesheet; detect game paths if missing."""
        self.qt_app.setStyle("Fusion")

        palette = QPalette()
        palette.setColor(QPalette.ColorRole.Window, QColor(PALETTE["ELEVATE_0"]))
        palette.setColor(QPalette.ColorRole.WindowText, QColor(PALETTE["TEXT_MAIN"]))
        palette.setColor(QPalette.ColorRole.Base, QColor(PALETTE["ELEVATE_1"]))
        palette.setColor(QPalette.ColorRole.AlternateBase, QColor(PALETTE["ELEVATE_2"]))
        palette.setColor(QPalette.ColorRole.Text, QColor(PALETTE["TEXT_MAIN"]))
        palette.setColor(QPalette.ColorRole.Button, QColor(PALETTE["ELEVATE_2"]))
        palette.setColor(QPalette.ColorRole.ButtonText, QColor(PALETTE["TEXT_MAIN"]))
        palette.setColor(QPalette.ColorRole.BrightText, QColor(PALETTE["TEXT_MAIN"]))
        palette.setColor(QPalette.ColorRole.Highlight, QColor(PALETTE["PRIMARY"]))
        palette.setColor(
            QPalette.ColorRole.HighlightedText, QColor(PALETTE["ELEVATE_0"])
        )
        palette.setColor(QPalette.ColorRole.ToolTipBase, QColor(PALETTE["ELEVATE_2"]))
        palette.setColor(QPalette.ColorRole.ToolTipText, QColor(PALETTE["TEXT_MAIN"]))
        self.qt_app.setPalette(palette)

        try:
            self.qt_app.setStyleSheet(get_stylesheet())
        except (FileNotFoundError, KeyError) as exc:
            logger.warning("Failed to load theme: {}", exc)

        cfg = load_config(config_file_path())
        if not cfg.paths.game:
            logger.info("No game path in config, attempting auto-detect")
            detected = detect_game_paths()
            if detected.game:
                cfg.paths = detected
                save_config(cfg)

        self._setup(cfg)

    def _setup(self, cfg: AppConfig) -> None:
        """Initialize CoreContext, services, and the main window via constructor DI."""
        self._ui_prefs = load_ui_prefs()
        self._ctx = CoreContext.create(cfg)
        self._ctx.add_rail_view(ModsViewPanel)
        self._ctx.register_plugin(SteamCmdUiPlugin())
        self.main_window = MainWindow(self._ctx, self._ui_prefs)

    async def async_run(self) -> int:
        """Start main window, prompt for game path if needed, then run event loop."""
        logger.info("Starting PxModRim")
        self.main_window.show()

        if not self._ctx.config.paths.game:
            logger.info("No game path found, showing settings dialog")
            result, dialog = await await_dialog(SettingsPanel, self._ctx)
            new_cfg = dialog.get_config()
            if result != 1 or not new_cfg.paths.game:
                logger.warning("No game path configured, exiting")
                return 1
            save_config(new_cfg)
            self._ctx.update_config(new_cfg)
            self._ctx.reset_providers(new_cfg.paths)
            self._ctx.steam_cmd_service.set_prefix(new_cfg.paths.steamcmd_prefix)

        await self._ctx.plugins.init_all(self._ctx)
        await self.main_window._refresh_mods()

        app_close_event = asyncio.Event()
        self.qt_app.aboutToQuit.connect(app_close_event.set)
        self.main_window.set_app_quit_callback(app_close_event.set)
        await app_close_event.wait()
        await self._ctx.plugins.shutdown_all()
        return 0

    def run(self) -> int:
        """Create a qasync event loop and run the application synchronously."""
        loop = QEventLoop()
        loop.set_exception_handler(_async_exception_handler)
        try:
            return asyncio.run(self.async_run(), loop_factory=lambda: loop)
        finally:
            self._ctx.close_sync()
