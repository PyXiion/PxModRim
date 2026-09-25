from __future__ import annotations

from datetime import UTC, datetime
from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QDialog,
    QHBoxLayout,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QMessageBox,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.core.mods_config import parse_mods_config
from pxmodrim.ui.components import AppButton


class RestoreSnapshotDialog(QDialog):
    """Let the user choose a saved mod list to restore."""

    def __init__(self, snapshots: list[Path], parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("restoreSnapshotDialog")
        self.setWindowTitle("Restore Mod List")
        self.resize(560, 420)

        self._snapshots = snapshots
        self._selected_snapshot: Path | None = None

        layout = QVBoxLayout(self)

        instruction = QLabel(
            "Choose a saved mod list. Your current list will be backed up before "
            "it is replaced.",
            self,
        )
        instruction.setWordWrap(True)
        layout.addWidget(instruction)

        self._list_widget = QListWidget(self)
        self._populate_list()
        layout.addWidget(self._list_widget)

        self._details_label = QLabel(self)
        self._details_label.setWordWrap(True)
        layout.addWidget(self._details_label)

        buttons = QHBoxLayout()
        buttons.addStretch()

        self._restore_btn = AppButton("Restore", self)
        self._restore_btn.setObjectName("primaryAction")
        self._restore_btn.setEnabled(False)
        self._restore_btn.clicked.connect(self._on_restore_clicked)

        cancel_btn = AppButton("Cancel", self)
        cancel_btn.clicked.connect(self.reject)

        buttons.addWidget(self._restore_btn)
        buttons.addWidget(cancel_btn)
        layout.addLayout(buttons)

        self._list_widget.currentItemChanged.connect(self._on_item_changed)
        self._list_widget.itemDoubleClicked.connect(self._on_item_double_clicked)

        if self._list_widget.count() > 0:
            self._list_widget.setCurrentRow(0)

    @property
    def selected_snapshot(self) -> Path | None:
        return self._selected_snapshot

    def _format_timestamp(self, path: Path) -> str:
        stem = path.stem
        if stem.startswith("ModsConfig_"):
            parts = stem[len("ModsConfig_") :].split("_")
            if len(parts) >= 2 and len(parts[0]) == 8 and len(parts[1]) >= 6:
                d, t = parts[0], parts[1][:6]
                try:
                    dt = datetime.strptime(f"{d}_{t}", "%Y%m%d_%H%M%S").replace(
                        tzinfo=UTC
                    )
                    return dt.strftime("%Y-%m-%d %H:%M:%S UTC")
                except ValueError:
                    pass
        try:
            modified = datetime.fromtimestamp(path.stat().st_mtime, tz=UTC)
            return modified.strftime("%Y-%m-%d %H:%M:%S UTC")
        except OSError:
            return stem

    def _populate_list(self) -> None:
        for path in self._snapshots:
            item = QListWidgetItem(self._list_widget)
            parsed = parse_mods_config(path)
            ts = self._format_timestamp(path)

            if parsed is not None:
                active_count = len(parsed.activeMods)
                noun = "mod" if active_count == 1 else "mods"
                text = (
                    f"{ts} — {active_count} active {noun}, RimWorld "
                    f"{parsed.version}\n   {path.name}"
                )
                item.setData(Qt.ItemDataRole.UserRole, (path, True, parsed))
            else:
                text = f"{ts} — Snapshot cannot be read\n   {path.name}"
                item.setData(Qt.ItemDataRole.UserRole, (path, False, None))

            item.setText(text)

    def _on_item_changed(
        self, current: QListWidgetItem | None, _: QListWidgetItem | None
    ) -> None:
        if current is None:
            self._selected_snapshot = None
            self._restore_btn.setEnabled(False)
            self._details_label.setText("")
            return

        path, is_valid, parsed = current.data(Qt.ItemDataRole.UserRole)
        if is_valid and parsed is not None:
            self._selected_snapshot = path
            self._restore_btn.setEnabled(True)
            self._details_label.setText(
                f"<b>File:</b> {path.name}<br>"
                f"<b>Active mods:</b> {len(parsed.activeMods)} | "
                f"<b>Game version:</b> {parsed.version}"
            )
        else:
            self._selected_snapshot = None
            self._restore_btn.setEnabled(False)
            warn_msg = (
                f"<b>This snapshot cannot be read.</b> "
                f"{path.name} will not be restored."
            )
            self._details_label.setText(warn_msg)

    def _on_restore_clicked(self) -> None:
        if self._selected_snapshot is not None:
            self.accept()

    def _on_item_double_clicked(self, _: QListWidgetItem) -> None:
        self._on_restore_clicked()


class ConfirmRestoreDialog(QMessageBox):
    """Ask for confirmation before replacing the current mod list."""

    def __init__(self, snapshot_name: str, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setIcon(QMessageBox.Icon.Question)
        self.setWindowTitle("Restore Mod List")
        self.setText(f"Restore the mod list saved in {snapshot_name}?")
        self.setInformativeText(
            "This replaces your current active mod list. PxModRim will save a "
            "backup of the current list first."
        )
        self.setStandardButtons(
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.Cancel
        )
        self.setDefaultButton(QMessageBox.StandardButton.Cancel)
        yes_btn = self.button(QMessageBox.StandardButton.Yes)
        if yes_btn:
            yes_btn.setText("Restore")
        cancel_btn = self.button(QMessageBox.StandardButton.Cancel)
        if cancel_btn:
            cancel_btn.setText("Cancel")
