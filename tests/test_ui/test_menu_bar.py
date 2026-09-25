from __future__ import annotations

from collections.abc import Iterator
from pathlib import Path

import pytest
from PySide6.QtGui import QCloseEvent
from PySide6.QtWidgets import QApplication, QMainWindow, QMenu

from pxmodrim.ui.window import menu_bar as menu_bar_module
from pxmodrim.ui.window.menu_bar import MenuBar


@pytest.fixture
def qapp() -> Iterator[QApplication]:
    app = QApplication.instance()
    if app is None:
        app = QApplication([])
    assert isinstance(app, QApplication)
    yield app


def test_help_menu_owns_open_logs_folder_action(qapp: QApplication) -> None:
    menu_bar = MenuBar()
    help_menu = next(
        menu for menu in menu_bar.findChildren(QMenu) if menu.title() == "&Help"
    )

    logs_action = next(
        action for action in help_menu.actions() if action.text() == "Open &Logs Folder"
    )

    assert logs_action.parent() is menu_bar


def test_open_logs_folder_uses_config_logs_path(
    qapp: QApplication, tmp_path: Path, monkeypatch
) -> None:
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    opened_urls = []
    monkeypatch.setattr(
        menu_bar_module.QDesktopServices,
        "openUrl",
        lambda url: opened_urls.append(url),
    )

    MenuBar._open_logs_folder()

    assert len(opened_urls) == 1
    assert Path(opened_urls[0].toLocalFile()) == tmp_path / "pxmodrim" / "logs"


def test_file_menu_quit_closes_owning_window(qapp: QApplication) -> None:
    class CloseTrackingWindow(QMainWindow):
        def __init__(self) -> None:
            super().__init__()
            self.close_path_invoked = False

        def closeEvent(self, event: QCloseEvent) -> None:
            self.close_path_invoked = True
            event.ignore()

    window = CloseTrackingWindow()
    menu_bar = MenuBar(window)
    window.setMenuBar(menu_bar)
    file_menu = next(
        menu for menu in menu_bar.findChildren(QMenu) if menu.title() == "&File"
    )
    quit_action = next(
        action for action in file_menu.actions() if action.text() == "&Quit"
    )

    quit_action.trigger()

    assert window.close_path_invoked is True


def test_file_menu_restore_snapshot_action(qapp: QApplication) -> None:
    menu_bar = MenuBar()
    file_menu = next(
        menu for menu in menu_bar.findChildren(QMenu) if menu.title() == "&File"
    )
    restore_action = next(
        action
        for action in file_menu.actions()
        if action.text() == "&Restore Mod List\u2026"
    )

    emissions: list[None] = []
    menu_bar.restore_snapshot_requested.connect(lambda: emissions.append(None))

    restore_action.trigger()

    assert emissions
