from __future__ import annotations

from collections.abc import Iterator

import pytest
from PySide6.QtGui import QKeySequence
from PySide6.QtWidgets import QApplication, QMainWindow, QMenu

from pxmodrim.ui.window.actions import (
    ACTION_SPECS,
    ActionId,
    create_actions,
    shortcut_rows,
)
from pxmodrim.ui.window.menu_bar import MENU_LAYOUT, MenuBar


@pytest.fixture
def qapp() -> Iterator[QApplication]:
    app = QApplication.instance()
    if app is None:
        app = QApplication([])
    assert isinstance(app, QApplication)
    yield app


def test_every_action_appears_in_exactly_one_menu() -> None:
    placed = [a for _, ids in MENU_LAYOUT for a in ids if a is not None]

    assert sorted(placed) == sorted(ActionId)


def test_shortcuts_are_unique() -> None:
    shortcuts = [spec.shortcut for spec in ACTION_SPECS if spec.shortcut]

    assert len(shortcuts) == len(set(shortcuts))


def test_shortcut_dialog_rows_have_no_duplicates_and_collapse_view_switching() -> None:
    rows = shortcut_rows()

    assert len(rows) == len(set(rows))
    assert sum(1 for _, label in rows if label == "Switch to view") == 1
    assert {label for _, label in rows} >= {"Save mod list", "Refresh mods"}


def test_menu_actions_carry_shortcuts_and_fire_while_menu_bar_hidden(
    qapp: QApplication,
) -> None:
    window = QMainWindow()
    actions = create_actions(window)
    menu_bar = MenuBar(actions, window)
    menu_bar.hide()
    triggered: list[bool] = []
    actions[ActionId.SAVE].triggered.connect(lambda: triggered.append(True))

    file_menu = next(
        menu for menu in menu_bar.findChildren(QMenu) if menu.title() == "&File"
    )
    save = next(a for a in file_menu.actions() if a.text() == "&Save Mod List")

    assert save.shortcut() == QKeySequence("Ctrl+S")
    assert save in window.actions()
    save.trigger()
    assert triggered == [True]
