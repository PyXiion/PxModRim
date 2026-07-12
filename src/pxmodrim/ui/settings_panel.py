from __future__ import annotations

from collections.abc import Callable
from pathlib import Path

from PySide6.QtWidgets import (
    QComboBox,
    QDialog,
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QLineEdit,
    QPushButton,
    QVBoxLayout,
)

from pxmodrim._compat.config import AppConfig, PathConfig, detect_game_paths


class SettingsPanel(QDialog):
    def __init__(self, cfg: AppConfig) -> None:
        super().__init__()
        self.setWindowTitle("Settings")
        self.setModal(True)
        self.resize(600, 300)

        self._config = cfg

        layout = QVBoxLayout(self)
        form = QFormLayout()

        self.game_edit, self.game_browse = self._add_path_row(
            form, "Game path:", cfg.paths.game, "Select RimWorld game folder", self._browse_game
        )
        self.local_edit, self.local_browse = self._add_path_row(
            form, "Local mods:", cfg.paths.local, "Select local mods folder", self._browse_local
        )
        self.workshop_edit, self.workshop_browse = self._add_path_row(
            form, "Workshop:", cfg.paths.workshop, "Select workshop mods folder", self._browse_workshop
        )
        self.config_edit, self.config_browse = self._add_path_row(
            form, "Config folder:", cfg.paths.config_folder, "Select RimWorld Config folder", self._browse_config
        )

        self.version_combo = QComboBox()
        for v in ["1.5", "1.4", "1.3"]:
            self.version_combo.addItem(v)
        idx = self.version_combo.findText(cfg.target_version)
        if idx >= 0:
            self.version_combo.setCurrentIndex(idx)
        form.addRow("Target version:", self.version_combo)

        layout.addLayout(form)

        buttons = QHBoxLayout()
        detect_btn = QPushButton("Auto-detect")
        detect_btn.clicked.connect(self._auto_detect)
        buttons.addWidget(detect_btn)
        buttons.addStretch()

        save_btn = QPushButton("Save")
        save_btn.clicked.connect(self._save)
        cancel_btn = QPushButton("Cancel")
        cancel_btn.clicked.connect(self.reject)
        buttons.addWidget(save_btn)
        buttons.addWidget(cancel_btn)
        layout.addLayout(buttons)

    def _add_path_row(
        self,
        form: QFormLayout,
        label: str,
        value: str,
        dialog_title: str,
        browse_handler: Callable[[], None],
    ) -> tuple[QLineEdit, QPushButton]:
        edit = QLineEdit(value)
        browse = QPushButton("Browse\u2026")
        browse.clicked.connect(browse_handler)
        row = QHBoxLayout()
        row.addWidget(edit, 1)
        row.addWidget(browse)
        form.addRow(label, row)
        return edit, browse

    def _browse_game(self) -> None:
        path = QFileDialog.getExistingDirectory(self, "Select RimWorld game folder")
        if path:
            self.game_edit.setText(path)
            self._auto_fill_local(path)

    def _browse_local(self) -> None:
        path = QFileDialog.getExistingDirectory(self, "Select local mods folder")
        if path:
            self.local_edit.setText(path)

    def _browse_workshop(self) -> None:
        path = QFileDialog.getExistingDirectory(self, "Select workshop mods folder")
        if path:
            self.workshop_edit.setText(path)

    def _browse_config(self) -> None:
        path = QFileDialog.getExistingDirectory(self, "Select RimWorld Config folder")
        if path:
            self.config_edit.setText(path)

    def _auto_fill_local(self, game_path: str) -> None:
        if not self.local_edit.text():
            candidate = Path(game_path) / "Mods"
            if candidate.is_dir():
                self.local_edit.setText(str(candidate))

    def _auto_detect(self) -> None:
        detected = detect_game_paths()
        self._apply_detected(detected)

    def _apply_detected(self, detected: PathConfig) -> None:
        for edit, val in [
            (self.game_edit, detected.game),
            (self.local_edit, detected.local),
            (self.workshop_edit, detected.workshop),
            (self.config_edit, detected.config_folder),
        ]:
            if val:
                edit.setText(val)

    def _save(self) -> None:
        self._config = AppConfig(
            paths=PathConfig(
                game=self.game_edit.text().strip(),
                local=self.local_edit.text().strip(),
                workshop=self.workshop_edit.text().strip(),
                config_folder=self.config_edit.text().strip(),
            ),
            target_version=self.version_combo.currentText(),
        )
        self.accept()

    def get_config(self) -> AppConfig:
        return self._config
