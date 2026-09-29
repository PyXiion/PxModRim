from __future__ import annotations

from PySide6.QtWidgets import QApplication, QMessageBox, QWidget

from pxmodrim.ui.plugins.organizer.dialogs import DeleteFolderDialog


def test_delete_folder_dialog_confirms_with_yes_button() -> None:
    app = QApplication.instance() or QApplication([])
    assert app is not None
    parent = QWidget()
    dialog = DeleteFolderDialog("Fixes", parent)
    yes = dialog.button(QMessageBox.StandardButton.Yes)
    assert yes is not None
    assert yes.text() == "Delete"
    yes.click()
    assert dialog.result() == QMessageBox.StandardButton.Yes
