from __future__ import annotations

from collections.abc import Iterator

import pytest
from PySide6.QtWidgets import QApplication, QDialog, QMessageBox, QPushButton

from pxmodrim.ui.components.dialog_chrome import install_dialog_chrome

_SB = QMessageBox.StandardButton


@pytest.fixture
def qapp() -> Iterator[QApplication]:
    app = QApplication.instance()
    if app is None:
        app = QApplication([])
    assert isinstance(app, QApplication)
    yield app


def _click_close(dialog: QDialog) -> None:
    install_dialog_chrome(dialog)
    button = dialog.findChild(QPushButton, "dialogCloseBtn")
    assert button is not None
    button.click()


@pytest.mark.parametrize(
    "buttons", [_SB.Yes | _SB.No | _SB.Cancel, _SB.Save | _SB.Discard | _SB.Cancel]
)
def test_message_box_close_yields_cancel(qapp: QApplication, buttons) -> None:
    box = QMessageBox()
    box.setStandardButtons(buttons)
    _click_close(box)
    assert box.result() == int(_SB.Cancel)


def test_plain_dialog_close_rejects(qapp: QApplication) -> None:
    dialog = QDialog()
    _click_close(dialog)
    assert dialog.result() == QDialog.DialogCode.Rejected
