from __future__ import annotations

from PySide6.QtCore import QUrl, Signal
from PySide6.QtGui import QAction, QDesktopServices
from PySide6.QtWidgets import QMenuBar, QWidget

from pxmodrim.core.config import config_dir


class MenuBar(QMenuBar):
    settings_requested = Signal()
    about_requested = Signal()
    restore_snapshot_requested = Signal()

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)

        file_menu = self.addMenu("&File")

        restore_action = QAction("&Restore Mod List\u2026", self)
        restore_action.triggered.connect(self.restore_snapshot_requested.emit)
        file_menu.addAction(restore_action)

        settings_action = QAction("&Settings\u2026", self)
        settings_action.triggered.connect(self.settings_requested.emit)
        file_menu.addAction(settings_action)
        file_menu.addSeparator()

        quit_action = QAction("&Quit", self)
        quit_action.triggered.connect(self._close_window)
        file_menu.addAction(quit_action)

        help_menu = self.addMenu("&Help")

        report_action = QAction("Report &Issue", self)
        report_action.triggered.connect(self._open_report)
        help_menu.addAction(report_action)

        logs_action = QAction("Open &Logs Folder", self)
        logs_action.triggered.connect(self._open_logs_folder)
        help_menu.addAction(logs_action)

        help_menu.addSeparator()

        about_action = QAction("&About PxModRim", self)
        about_action.triggered.connect(self.about_requested.emit)
        help_menu.addAction(about_action)

    def _close_window(self) -> None:
        self.window().close()

    @staticmethod
    def _open_report() -> None:
        QDesktopServices.openUrl(QUrl("https://github.com/PyXiion/PxModRim/issues"))

    @staticmethod
    def _open_logs_folder() -> None:
        QDesktopServices.openUrl(QUrl.fromLocalFile(str(config_dir() / "logs")))
