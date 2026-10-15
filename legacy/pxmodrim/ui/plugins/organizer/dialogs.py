from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QDialog,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMessageBox,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.core.organizer import ROOT_ID, FolderNode
from pxmodrim.ui.components.button import AppButton


class FolderNameDialog(QDialog):
    def __init__(
        self, title: str, initial: str, parent: QWidget, verb: str | None = None
    ) -> None:
        super().__init__(parent)
        self.setWindowTitle(title)
        self.setMinimumWidth(360)
        layout = QVBoxLayout(self)
        layout.addWidget(QLabel("Folder name", self))
        self._edit = QLineEdit(initial, self)
        layout.addWidget(self._edit)
        primary_text = verb or title.split()[0]
        self._ok = AppButton(primary_text, self)
        self._ok.setObjectName("primaryAction")
        self._ok.setDefault(True)
        cancel = AppButton("Cancel", self)
        self._ok.clicked.connect(self.accept)
        cancel.clicked.connect(self.reject)
        row = QHBoxLayout()
        row.addStretch(1)
        row.addWidget(cancel)
        row.addWidget(self._ok)
        layout.addLayout(row)
        self._edit.textChanged.connect(self._validate)
        self._edit.returnPressed.connect(self._ok.click)
        self._validate(initial)
        self._edit.selectAll()

    def _validate(self, text: str) -> None:
        self._ok.setEnabled(bool(text.strip()))

    def textValue(self) -> str:
        return self._edit.text().strip()


class DeleteFolderDialog(QMessageBox):
    def __init__(self, name: str, parent: QWidget) -> None:
        super().__init__(parent)
        self.setIcon(QMessageBox.Icon.Warning)
        self.setWindowTitle("Delete Folder")
        self.setText(f"Delete ‘{name}’ and its subfolders?")
        self.setInformativeText(
            "Their mods become ungrouped. Load order is not affected."
        )
        self.setStandardButtons(
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.Cancel
        )
        self.setDefaultButton(QMessageBox.StandardButton.Cancel)
        self.setEscapeButton(QMessageBox.StandardButton.Cancel)
        yes = self.button(QMessageBox.StandardButton.Yes)
        if yes:
            yes.setText("Delete")
            yes.setObjectName("primaryAction")


class FolderPickerDialog(QDialog):
    def __init__(
        self,
        tree: FolderNode,
        title: str,
        allowed: frozenset[int],
        parent: QWidget,
        verb: str = "Move",
    ) -> None:
        super().__init__(parent)
        self.setWindowTitle(title)
        self.resize(380, 450)
        layout = QVBoxLayout(self)
        layout.addWidget(QLabel("Destination folder"))
        self._tree = QTreeWidget(self)
        self._tree.setHeaderHidden(True)
        self._tree.setRootIsDecorated(True)
        layout.addWidget(self._tree)
        top = QTreeWidgetItem(self._tree, ["Ungrouped (top level)"])
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
        self._ok = AppButton(verb, self)
        self._ok.setObjectName("primaryAction")
        self._ok.setDefault(True)
        self._ok.setEnabled(False)
        cancel = AppButton("Cancel", self)
        self._tree.currentItemChanged.connect(self._update_ok)
        self._tree.itemDoubleClicked.connect(
            lambda _item, _column: self._accept_valid()
        )
        self._ok.clicked.connect(self._accept_valid)
        cancel.clicked.connect(self.reject)
        row = QHBoxLayout()
        row.addStretch(1)
        row.addWidget(cancel)
        row.addWidget(self._ok)
        layout.addLayout(row)

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
