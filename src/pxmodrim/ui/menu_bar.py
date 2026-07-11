from __future__ import annotations

from PySide6.QtCore import Signal
from PySide6.QtGui import QAction
from PySide6.QtWidgets import QMenuBar


class MenuBar(QMenuBar):
    settings_requested = Signal()
    about_requested = Signal()

    def __init__(self) -> None:
        super().__init__()

        file_menu = self.addMenu("&File")

        settings_action = QAction("&Settings\u2026", self)
        settings_action.setShortcut("Ctrl+,")
        settings_action.triggered.connect(self.settings_requested.emit)
        file_menu.addAction(settings_action)

        file_menu.addSeparator()

        quit_action = QAction("&Quit", self)
        quit_action.setShortcut("Ctrl+Q")
        quit_action.triggered.connect(self._quit)
        file_menu.addAction(quit_action)

        help_menu = self.addMenu("&Help")
        about_action = QAction("&About PxModRim", self)
        about_action.triggered.connect(self.about_requested.emit)
        help_menu.addAction(about_action)

    @staticmethod
    def _quit() -> None:
        from PySide6.QtWidgets import QApplication

        QApplication.quit()
