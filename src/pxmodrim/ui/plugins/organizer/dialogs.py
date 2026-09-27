from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QDialog,
    QDialogButtonBox,
    QInputDialog,
    QLabel,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.core.organizer import ROOT_ID, FolderNode


class FolderNameDialog(QInputDialog):
    def __init__(self, title: str, initial: str, parent: QWidget) -> None:
        super().__init__(parent)
        self.setWindowTitle(title)
        self.setLabelText("Folder name:")
        self.setTextValue(initial)


class FolderPickerDialog(QDialog):
    def __init__(
        self,
        tree: FolderNode,
        title: str,
        allowed: frozenset[int],
        parent: QWidget,
    ) -> None:
        super().__init__(parent)
        self.setWindowTitle(title)
        self.resize(380, 450)
        layout = QVBoxLayout(self)
        layout.addWidget(QLabel("Choose a destination folder:"))
        self._tree = QTreeWidget(self)
        self._tree.setHeaderHidden(True)
        self._tree.setRootIsDecorated(True)
        layout.addWidget(self._tree)
        top = QTreeWidgetItem(self._tree, ["Top level / Ungrouped"])
        top.setData(0, Qt.ItemDataRole.UserRole, ROOT_ID)
        if ROOT_ID not in allowed:
            top.setDisabled(True)

        def add(parent: QTreeWidgetItem, node: FolderNode) -> None:
            item = QTreeWidgetItem(parent, [node.folder.name])
            item.setData(0, Qt.ItemDataRole.UserRole, node.folder.id)
            if node.folder.id not in allowed:
                item.setDisabled(True)
            for child in node.children:
                add(item, child)

        for folder in tree.children:
            add(top, folder)
        self._tree.expandAll()
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel,
            parent=self,
        )
        self._ok = buttons.button(QDialogButtonBox.StandardButton.Ok)
        self._ok.setEnabled(False)
        self._tree.currentItemChanged.connect(self._update_ok)
        self._tree.itemDoubleClicked.connect(
            lambda _item, _column: self._accept_valid()
        )
        buttons.accepted.connect(self._accept_valid)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

    def _update_ok(
        self, item: QTreeWidgetItem | None, _previous: QTreeWidgetItem | None
    ) -> None:
        self._ok.setEnabled(item is not None and not item.isDisabled())

    def _accept_valid(self) -> None:
        if self._ok.isEnabled():
            self.accept()

    @property
    def folder_id(self) -> int:
        item = self._tree.currentItem()
        assert item is not None
        return int(item.data(0, Qt.ItemDataRole.UserRole))
