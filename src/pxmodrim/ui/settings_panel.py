from __future__ import annotations

from collections.abc import Callable
from pathlib import Path

from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDialog,
    QFileDialog,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLineEdit,
    QPushButton,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)
from qasync import asyncSlot

from pxmodrim._compat.config import (
    AppConfig,
    PathConfig,
    community_rules_file,
    detect_game_paths,
)
from pxmodrim.core.loading import LoadingState
from pxmodrim.sort.community_service import CommunityRulesService
from pxmodrim.sort.config import SortMethod, SortSettings, TierConfig
from pxmodrim.ui.components.progress_dialog import ProgressDialog


class SettingsPanel(QDialog):
    def __init__(self, cfg: AppConfig) -> None:
        super().__init__()
        self.setWindowTitle("Settings")
        self.setModal(True)
        self.resize(700, 500)

        self._config = cfg

        layout = QVBoxLayout(self)
        tabs = QTabWidget()
        layout.addWidget(tabs)

        # Locations tab
        tabs.addTab(self._create_locations_tab(), "Locations")

        # Sorting tab
        tabs.addTab(self._create_sorting_tab(), "Sorting")

        # Buttons
        buttons = QHBoxLayout()
        buttons.addStretch()

        save_btn = QPushButton("Save")
        save_btn.clicked.connect(self._save)
        cancel_btn = QPushButton("Cancel")
        cancel_btn.clicked.connect(self.reject)
        buttons.addWidget(save_btn)
        buttons.addWidget(cancel_btn)
        layout.addLayout(buttons)

    # ── Locations tab ────────────────────────────────────────────────────────

    def _create_locations_tab(self) -> QWidget:
        tab = QWidget()
        form = QFormLayout(tab)

        self.game_edit, self.game_browse = self._add_path_row(
            form,
            "Game path:",
            self._config.paths.game,
            "Select RimWorld game folder",
            self._browse_game,
        )
        self.local_edit, self.local_browse = self._add_path_row(
            form,
            "Local mods:",
            self._config.paths.local,
            "Select local mods folder",
            self._browse_local,
        )
        self.workshop_edit, self.workshop_browse = self._add_path_row(
            form,
            "Workshop:",
            self._config.paths.workshop,
            "Select workshop mods folder",
            self._browse_workshop,
        )
        self.config_edit, self.config_browse = self._add_path_row(
            form,
            "Config folder:",
            self._config.paths.config_folder,
            "Select RimWorld Config folder",
            self._browse_config,
        )

        self.version_combo = QComboBox()
        for v in ["1.6", "1.5", "1.4", "1.3"]:
            self.version_combo.addItem(v)
        idx = self.version_combo.findText(self._config.target_version)
        if idx >= 0:
            self.version_combo.setCurrentIndex(idx)
        form.addRow("Target version:", self.version_combo)

        detect_btn = QPushButton("Auto-detect")
        detect_btn.clicked.connect(self._auto_detect)
        form.addRow("", detect_btn)

        return tab

    # ── Sorting tab ──────────────────────────────────────────────────────────

    def _create_sorting_tab(self) -> QWidget:
        tab = QWidget()
        layout = QVBoxLayout(tab)

        # Algorithm group
        algo_group = QGroupBox("Algorithm")
        algo_layout = QVBoxLayout(algo_group)

        self.sort_method = QComboBox()
        self.sort_method.addItems(["Topological"])
        if self._config.sort.method == SortMethod.TOPOLOGICAL:
            self.sort_method.setCurrentIndex(0)
        algo_layout.addWidget(self.sort_method)

        layout.addWidget(algo_group)

        # Options group
        opts_group = QGroupBox("Options")
        opts_layout = QVBoxLayout(opts_group)

        self.use_moddeps_cb = QCheckBox("Use modDependencies as loadBefore")
        self.use_moddeps_cb.setChecked(
            self._config.sort.use_moddependencies_as_load_before
        )
        opts_layout.addWidget(self.use_moddeps_cb)

        self.use_alt_ids_cb = QCheckBox("Use alternativePackageIds")
        self.use_alt_ids_cb.setChecked(self._config.sort.use_alternative_package_ids)
        opts_layout.addWidget(self.use_alt_ids_cb)

        self.check_missing_cb = QCheckBox("Check missing dependencies")
        self.check_missing_cb.setChecked(self._config.sort.check_missing_dependencies)
        opts_layout.addWidget(self.check_missing_cb)

        self.use_community_cb = QCheckBox("Use community rules database")
        self.use_community_cb.setChecked(self._config.sort.use_community_rules)
        opts_layout.addWidget(self.use_community_cb)

        layout.addWidget(opts_group)

        # Community rules group
        cr_group = QGroupBox("Community Rules Database")
        cr_layout = QVBoxLayout(cr_group)

        cr_path = community_rules_file()
        self.cr_status = QLineEdit()
        self.cr_status.setReadOnly(True)
        if cr_path.exists():
            self.cr_status.setText(f"Found: {cr_path}")
        else:
            self.cr_status.setText("Not downloaded")
        cr_layout.addWidget(self.cr_status)

        self.cr_download_btn = QPushButton("Download / Update")
        self.cr_download_btn.clicked.connect(self._download_community_rules)
        cr_layout.addWidget(self.cr_download_btn)

        layout.addWidget(cr_group)

        layout.addStretch()
        return tab

    # ── Helpers ──────────────────────────────────────────────────────────────

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

    @asyncSlot()
    async def _download_community_rules(self) -> None:
        self.cr_download_btn.setEnabled(False)
        self.cr_status.setText("Downloading...")
        try:
            async with ProgressDialog(LoadingState(self)) as dialog:
                service = CommunityRulesService()
                path = await service.ensure_rules(dialog.loading, force=True)

            if path:
                self.cr_status.setText(f"Downloaded: {path}")
            else:
                self.cr_status.setText("Download failed")
        finally:
            self.cr_download_btn.setEnabled(True)

    def _save(self) -> None:
        self._config = AppConfig(
            paths=PathConfig(
                game=self.game_edit.text().strip(),
                local=self.local_edit.text().strip(),
                workshop=self.workshop_edit.text().strip(),
                config_folder=self.config_edit.text().strip(),
            ),
            target_version=self.version_combo.currentText(),
            sort=SortSettings(
                method=SortMethod.TOPOLOGICAL,
                use_moddependencies_as_load_before=self.use_moddeps_cb.isChecked(),
                use_alternative_package_ids=self.use_alt_ids_cb.isChecked(),
                check_missing_dependencies=self.check_missing_cb.isChecked(),
                use_community_rules=self.use_community_cb.isChecked(),
                tier_config=TierConfig.default(),
            ),
        )
        self.accept()

    def get_config(self) -> AppConfig:
        return self._config
