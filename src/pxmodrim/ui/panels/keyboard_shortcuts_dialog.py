from __future__ import annotations

from PySide6.QtWidgets import (
    QDialog,
    QHBoxLayout,
    QHeaderView,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.ui.components.button import AppButton

QML_SHORTCUTS = (
    ("Up / Down", "Navigate mod list"),
    ("Shift+Up / Shift+Down", "Extend mod selection"),
    ("Return / Space", "Toggle selected mods"),
    ("Ctrl+A", "Select all mods"),
)


class KeyboardShortcutsDialog(QDialog):
    def __init__(
        self, rows: tuple[tuple[str, str], ...], parent: QWidget | None = None
    ) -> None:
        super().__init__(parent)
        self.setWindowTitle("Keyboard Shortcuts")
        self.resize(520, 600)
        layout = QVBoxLayout(self)
        table = QTableWidget(len(rows), 2, self)
        table.setHorizontalHeaderLabels(("Shortcut", "Action"))
        table.verticalHeader().hide()
        table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        table.setSelectionMode(QTableWidget.SelectionMode.NoSelection)
        table.horizontalHeader().setSectionResizeMode(
            0, QHeaderView.ResizeMode.ResizeToContents
        )
        table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        for row, (shortcut, action) in enumerate(rows):
            table.setItem(row, 0, QTableWidgetItem(shortcut))
            table.setItem(row, 1, QTableWidgetItem(action))
        layout.addWidget(table)

        buttons = QHBoxLayout()
        buttons.addStretch()
        close_btn = AppButton("Close", self)
        close_btn.setObjectName("primaryAction")
        close_btn.setDefault(True)
        close_btn.clicked.connect(self.accept)
        buttons.addWidget(close_btn)
        layout.addLayout(buttons)
