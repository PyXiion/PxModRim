from __future__ import annotations

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
        self.resize(500, 250)

        self._config = cfg

        layout = QVBoxLayout(self)
        form = QFormLayout()

        self.game_edit = QLineEdit(cfg.paths.game)
        self.game_browse = QPushButton("Browse\u2026")
        self.game_browse.clicked.connect(self._browse_game)
        game_row = QHBoxLayout()
        game_row.addWidget(self.game_edit, 1)
        game_row.addWidget(self.game_browse)
        form.addRow("Game path:", game_row)

        self.local_edit = QLineEdit(cfg.paths.local)
        self.local_browse = QPushButton("Browse\u2026")
        self.local_browse.clicked.connect(self._browse_local)
        local_row = QHBoxLayout()
        local_row.addWidget(self.local_edit, 1)
        local_row.addWidget(self.local_browse)
        form.addRow("Local mods:", local_row)

        self.workshop_edit = QLineEdit(cfg.paths.workshop)
        self.workshop_browse = QPushButton("Browse\u2026")
        self.workshop_browse.clicked.connect(self._browse_workshop)
        workshop_row = QHBoxLayout()
        workshop_row.addWidget(self.workshop_edit, 1)
        workshop_row.addWidget(self.workshop_browse)
        form.addRow("Workshop:", workshop_row)

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

    def _auto_fill_local(self, game_path: str) -> None:
        if not self.local_edit.text():
            candidate = Path(game_path) / "Mods"
            if candidate.is_dir():
                self.local_edit.setText(str(candidate))

    def _auto_detect(self) -> None:
        detected = detect_game_paths()
        if detected.game:
            self.game_edit.setText(detected.game)
        if detected.local:
            self.local_edit.setText(detected.local)
        if detected.workshop:
            self.workshop_edit.setText(detected.workshop)

    def _save(self) -> None:
        self._config = AppConfig(
            paths=PathConfig(
                game=self.game_edit.text().strip(),
                local=self.local_edit.text().strip(),
                workshop=self.workshop_edit.text().strip(),
            ),
            target_version=self.version_combo.currentText(),
        )
        self.accept()

    def get_config(self) -> AppConfig:
        return self._config
