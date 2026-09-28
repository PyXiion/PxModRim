from __future__ import annotations

from collections.abc import Callable
from pathlib import Path

from PySide6.QtCore import QTimer
from PySide6.QtWidgets import (
    QCheckBox,
    QDialog,
    QFileDialog,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)
from qasync import asyncSlot

from pxmodrim.core.config import (
    AppConfig,
    PathConfig,
    community_rules_file,
    detect_game_paths,
)
from pxmodrim.core.context import CoreContext
from pxmodrim.core.loading import LoadingState
from pxmodrim.core.sort.community_service import CommunityRulesService
from pxmodrim.core.sort.config import SortSettings
from pxmodrim.ui.components import AppButton
from pxmodrim.ui.components.dialogs import await_dialog
from pxmodrim.ui.components.progress_dialog import ProgressDialog


class _FolderDialog(QFileDialog):
    def __init__(self, parent: QWidget, title: str) -> None:
        super().__init__(parent, title)
        self.setFileMode(QFileDialog.FileMode.Directory)
        self.setOption(QFileDialog.Option.ShowDirsOnly)


class SettingsPanel(QDialog):
    def __init__(self, ctx: CoreContext, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("settingsPanel")
        self.setWindowTitle("Settings")
        self.setModal(True)
        self.resize(700, 500)

        self._ctx = ctx
        self._config = ctx.config

        layout = QVBoxLayout(self)
        tabs = QTabWidget(self)
        layout.addWidget(tabs)

        tabs.addTab(self._create_general_tab(), "General")
        tabs.addTab(self._create_sorting_tab(), "Sorting")

        buttons = QHBoxLayout()
        buttons.addStretch()

        cancel_btn = AppButton("Cancel", self)
        cancel_btn.clicked.connect(self.reject)
        buttons.addWidget(cancel_btn)

        save_btn = AppButton("Save", self)
        save_btn.setObjectName("primaryAction")
        save_btn.clicked.connect(self._save)
        buttons.addWidget(save_btn)
        layout.addLayout(buttons)

    # ── General tab ──────────────────────────────────────────────────────────

    def _create_general_tab(self) -> QWidget:
        tab = QWidget(self)
        layout = QVBoxLayout(tab)

        folders_group = QGroupBox("Folders", tab)
        form = QFormLayout(folders_group)

        self.game_edit, self.game_browse = self._add_path_row(
            form,
            "Game folder:",
            self._config.paths.game,
            "Select RimWorld Game Folder",
            after_pick=self._auto_fill_local,
        )
        self.local_edit, self.local_browse = self._add_path_row(
            form,
            "Local mods folder:",
            self._config.paths.local,
            "Select Local Mods Folder",
        )
        self.workshop_edit, self.workshop_browse = self._add_path_row(
            form,
            "Workshop mods folder:",
            self._config.paths.workshop,
            "Select Workshop Mods Folder",
        )
        self.config_edit, self.config_browse = self._add_path_row(
            form,
            "Config folder:",
            self._config.paths.config_folder,
            "Select RimWorld Config Folder",
        )

        detect_btn = AppButton("Auto-detect", folders_group)
        detect_btn.clicked.connect(self._auto_detect)
        form.addRow("", detect_btn)
        layout.addWidget(folders_group)

        appearance_group = QGroupBox("Appearance", tab)
        appearance_layout = QVBoxLayout(appearance_group)
        self.compact_mod_list_cb = QCheckBox("Compact mod list", appearance_group)
        self.compact_mod_list_cb.setChecked(self._config.compact_mod_list)
        appearance_layout.addWidget(self.compact_mod_list_cb)
        layout.addWidget(appearance_group)

        layout.addStretch()
        return tab

    # ── Sorting tab ──────────────────────────────────────────────────────────

    def _create_sorting_tab(self) -> QWidget:
        tab = QWidget(self)
        layout = QVBoxLayout(tab)

        opts_group = QGroupBox("Sorting options", tab)
        opts_layout = QVBoxLayout(opts_group)

        self.use_alt_ids_cb = QCheckBox("Use alternative package IDs", opts_group)
        self.use_alt_ids_cb.setChecked(self._config.sort.use_alternative_package_ids)
        opts_layout.addWidget(self.use_alt_ids_cb)

        self.check_missing_cb = QCheckBox("Check missing dependencies", opts_group)
        self.check_missing_cb.setChecked(self._config.sort.check_missing_dependencies)
        opts_layout.addWidget(self.check_missing_cb)

        self.use_community_cb = QCheckBox("Use community rules database", opts_group)
        self.use_community_cb.setChecked(self._config.sort.use_community_rules)
        opts_layout.addWidget(self.use_community_cb)
        layout.addWidget(opts_group)

        cr_group = QGroupBox("Community rules database", tab)
        cr_layout = QVBoxLayout(cr_group)

        cr_path = community_rules_file()
        self.cr_status = QLabel(
            f"Found: {cr_path}" if cr_path.exists() else "Not downloaded", cr_group
        )
        self.cr_status.setWordWrap(True)
        cr_layout.addWidget(self.cr_status)

        self.cr_download_btn = AppButton("Download or update", cr_group)
        self.cr_download_btn.clicked.connect(self._download_community_rules)
        cr_layout.addWidget(self.cr_download_btn)
        layout.addWidget(cr_group)

        si_group = QGroupBox("Startup impact cache", tab)
        si_layout = QVBoxLayout(si_group)

        self.si_status = QLabel(si_group)
        self.si_status.setWordWrap(True)
        si_layout.addWidget(self.si_status)

        self.si_clear_btn = AppButton("Clear cache", si_group)
        self.si_clear_btn.clicked.connect(self._clear_startup_impact)
        si_layout.addWidget(self.si_clear_btn)

        if self._ctx.config.paths.config_folder:
            self.si_status.setText("Cache ready")
        else:
            self.si_status.setText("No config folder — not available")
            self.si_clear_btn.setEnabled(False)
        layout.addWidget(si_group)

        layout.addStretch()
        return tab

    # ── Helpers ──────────────────────────────────────────────────────────────

    def _add_path_row(
        self,
        form: QFormLayout,
        label: str,
        value: str,
        dialog_title: str,
        after_pick: Callable[[str], None] | None = None,
    ) -> tuple[QLineEdit, AppButton]:
        parent = form.parentWidget()
        edit = QLineEdit(value, parent)
        browse = AppButton("Browse…", parent)
        browse.clicked.connect(
            lambda: self._browse_folder(edit, dialog_title, after_pick)
        )
        row = QHBoxLayout()
        row.addWidget(edit, 1)
        row.addWidget(browse)
        form.addRow(label, row)
        return edit, browse

    @asyncSlot()
    async def _browse_folder(
        self,
        edit: QLineEdit,
        title: str,
        after_pick: Callable[[str], None] | None,
    ) -> None:
        result, dialog = await await_dialog(_FolderDialog, self, title)
        files = dialog.selectedFiles()
        if result != QDialog.DialogCode.Accepted or not files:
            return
        edit.setText(files[0])
        if after_pick is not None:
            after_pick(files[0])

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
        self.cr_status.setText("Downloading…")
        try:
            async with ProgressDialog(LoadingState(self), self) as dialog:
                service = CommunityRulesService(self._ctx.config_service)
                path = await service.ensure_rules(dialog.loading, force=True)

            QTimer.singleShot(1000, dialog, dialog.deleteLater)

            if path:
                self.cr_status.setText(f"Downloaded: {path}")
            else:
                self.cr_status.setText("Download failed")
        finally:
            self.cr_download_btn.setEnabled(True)

    @asyncSlot()
    async def _clear_startup_impact(self) -> None:
        self.si_clear_btn.setEnabled(False)
        self.si_status.setText("Clearing…")
        try:
            await self._ctx.mod_service.startup_impact.clear()
            self.si_status.setText("Cache cleared")
        except OSError as exc:
            self.si_status.setText(f"Error: {exc}")
        finally:
            self.si_clear_btn.setEnabled(True)

    def _save(self) -> None:
        self._config = AppConfig(
            paths=PathConfig(
                game=self.game_edit.text().strip(),
                local=self.local_edit.text().strip(),
                workshop=self.workshop_edit.text().strip(),
                config_folder=self.config_edit.text().strip(),
            ),
            sort=SortSettings(
                use_alternative_package_ids=self.use_alt_ids_cb.isChecked(),
                check_missing_dependencies=self.check_missing_cb.isChecked(),
                use_community_rules=self.use_community_cb.isChecked(),
            ),
            max_snapshots=self._config.max_snapshots,
            compact_mod_list=self.compact_mod_list_cb.isChecked(),
        )
        self.accept()

    def get_config(self) -> AppConfig:
        return self._config
