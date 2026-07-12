from __future__ import annotations

import asyncio
import sys
import traceback
from types import TracebackType

from loguru import logger
from PySide6.QtWidgets import QApplication, QMessageBox
from qasync import QEventLoop

from pxmodrim._compat.config import (
    AppConfig,
    config_file_path,
    detect_game_paths,
    load_config,
    save_config,
)
from pxmodrim._compat.dialogs import await_dialog
from pxmodrim.core.context import CoreContext
from pxmodrim.core.mod_service import ModService
from pxmodrim.core.providers import create_providers
from pxmodrim.ui.main_window import MainWindow
from pxmodrim.ui.palette import get_stylesheet
from pxmodrim.ui.settings_panel import SettingsPanel


def _exception_hook(
    exc_type: type[BaseException],
    exc_value: BaseException,
    exc_tb: TracebackType | None,
) -> None:
    """Global exception handler for unhandled exceptions."""
    error_msg = "".join(traceback.format_exception(exc_type, exc_value, exc_tb))
    logger.error("Unhandled exception:\n{}", error_msg)
    app = QApplication.instance()
    if app is not None:
        QMessageBox.critical(
            None,
            "PxModRim - Crash",
            f"An unexpected error occurred:\n\n{exc_type.__name__}: {exc_value}\n\nDetails have been logged.",
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
    def __init__(self) -> None:
        self.qt_app = QApplication(sys.argv)
        self.qt_app.setApplicationName("PxModRim")
        self.qt_app.setOrganizationName("PxModRim")
        self.qt_app.setApplicationDisplayName("PxModRim")

        self._apply_theme()

    def _apply_theme(self) -> None:
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
        self._ctx = CoreContext(cfg)
        providers = create_providers(cfg.paths)
        self._mod_service = ModService(self._ctx, providers)
        self.main_window = MainWindow(self._ctx, self._mod_service)

    async def async_run(self) -> int:
        logger.info("Starting PxModRim")
        self.main_window.show()

        if not self._ctx.config.paths.game:
            logger.info("No game path found, showing settings dialog")
            result, dialog = await await_dialog(SettingsPanel, self._ctx.config)
            new_cfg = dialog.get_config()
            if result != 1 or not new_cfg.paths.game:
                logger.warning("No game path configured, exiting")
                return 1
            save_config(new_cfg)
            self._ctx.update_config(new_cfg)
            self._mod_service.reset_providers(create_providers(new_cfg.paths))

        await self.main_window.load_mods_async()

        app_close_event = asyncio.Event()
        self.qt_app.aboutToQuit.connect(app_close_event.set)
        await app_close_event.wait()
        return 0

    def run(self) -> int:
        loop = QEventLoop()
        loop.set_exception_handler(_async_exception_handler)
        return asyncio.run(self.async_run(), loop_factory=lambda: loop)
