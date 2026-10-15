from __future__ import annotations

from collections.abc import Iterator

import pytest
from PySide6.QtWidgets import QApplication, QMessageBox, QWidget

from pxmodrim.ui.plugins.organizer.dialogs import DeleteFolderDialog


@pytest.fixture(scope="module")
def qapp() -> Iterator[QApplication]:
    app = QApplication.instance() or QApplication([])
    assert isinstance(app, QApplication)
    yield app


def test_delete_folder_dialog_confirms_with_yes_button(qapp: QApplication) -> None:
    parent = QWidget()
    dialog = DeleteFolderDialog("Fixes", parent)
    yes = dialog.button(QMessageBox.StandardButton.Yes)
    assert yes is not None
    assert yes.text() == "Delete"
    yes.click()
    assert dialog.result() == QMessageBox.StandardButton.Yes
