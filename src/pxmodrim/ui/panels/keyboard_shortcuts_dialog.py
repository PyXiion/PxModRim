from __future__ import annotations

from PySide6.QtWidgets import (
    QDialog,
    QHeaderView,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

QML_SHORTCUTS = (
    ("Up", "Navigate mod list"),
    ("Down", "Navigate mod list"),
    ("Shift+Up", "Extend mod selection"),
    ("Shift+Down", "Extend mod selection"),
    ("Return", "Toggle selected mod(s)"),
    ("Space", "Toggle selected mod(s)"),
    ("Ctrl+A", "Select all mods"),
)


class KeyboardShortcutsDialog(QDialog):
    def __init__(
        self, rows: tuple[tuple[str, str], ...], parent: QWidget | None = None
    ) -> None:
        super().__init__(parent)
        self.setWindowTitle("Keyboard Shortcuts")
        self.resize(520, 480)
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
