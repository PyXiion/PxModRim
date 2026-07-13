from __future__ import annotations

from PySide6.QtCore import QUrl, Signal
from PySide6.QtGui import QAction, QDesktopServices
from PySide6.QtWidgets import QMenuBar


class MenuBar(QMenuBar):
    settings_requested = Signal()
    about_requested = Signal()

    def __init__(self) -> None:
        super().__init__()

        file_menu = self.addMenu("&File")

        settings_action = QAction("&Settings\u2026", self)
        settings_action.triggered.connect(self.settings_requested.emit)
        file_menu.addAction(settings_action)

        file_menu.addSeparator()

        quit_action = QAction("&Quit", self)
        quit_action.triggered.connect(self._quit)
        file_menu.addAction(quit_action)

        help_menu = self.addMenu("&Help")

        report_action = QAction("Report &Issue", self)
        report_action.triggered.connect(self._open_report)
        help_menu.addAction(report_action)

        help_menu.addSeparator()

        about_action = QAction("&About PxModRim", self)
        about_action.triggered.connect(self.about_requested.emit)
        help_menu.addAction(about_action)

    @staticmethod
    def _quit() -> None:
        from PySide6.QtWidgets import QApplication

        QApplication.quit()

    @staticmethod
    def _open_report() -> None:
        QDesktopServices.openUrl(QUrl("https://github.com/PyXiion/PxModRim/issues"))
