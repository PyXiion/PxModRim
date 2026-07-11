from __future__ import annotations

from loguru import logger
from PySide6.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QStatusBar,
    QVBoxLayout,
    QWidget,
)
from qasync import asyncSlot

from pxmodrim._compat.config import save_config
from pxmodrim._compat.dialogs import await_dialog
from pxmodrim.core.context import CoreContext
from pxmodrim.core.mod_service import ModService
from pxmodrim.models.metadata.structures import ListedMod
from pxmodrim.ui.menu_bar import MenuBar
from pxmodrim.ui.mod_info_panel import ModInfoPanel
from pxmodrim.ui.mod_list_panel import ModListPanel
from pxmodrim.ui.settings_panel import SettingsPanel
from pxmodrim.ui.sidebar_panel import (
    ActiveModsEntry,
    AllModsEntry,
    ErrorModsEntry,
    InactiveModsEntry,
    ProviderModsEntry,
    SidebarEntry,
    SidebarPanel,
)

PROVIDER_LABELS: dict[str, str] = {
    "local": "Local",
    "steam_cmd": "Steam CMD",
    "core": "System / Core",
}


class MainWindow(QMainWindow):
    def __init__(self, ctx: CoreContext, mod_service: ModService) -> None:
        super().__init__()
        self.setWindowTitle("PxModRim")
        self.resize(1400, 850)

        self._ctx = ctx
        self._mod_service = mod_service

        # Menu bar
        menu_bar = MenuBar()
        menu_bar.settings_requested.connect(self._open_settings)
        menu_bar.about_requested.connect(self._show_about)
        self.setMenuBar(menu_bar)

        # Outer container: header + content
        outer = QWidget()
        outer_layout = QVBoxLayout(outer)
        outer_layout.setContentsMargins(0, 0, 0, 0)
        outer_layout.setSpacing(0)

        # Header
        header = self._create_header()
        outer_layout.addWidget(header)

        # Content (3-panel workspace)
        content = QWidget()
        h_layout = QHBoxLayout(content)
        h_layout.setContentsMargins(0, 0, 0, 0)
        h_layout.setSpacing(0)

        self.sidebar = SidebarPanel()
        self.sidebar.setFixedWidth(200)
        self.sidebar.entry_selected.connect(self._on_entry_selected)
        h_layout.addWidget(self.sidebar)

        self.mod_list = ModListPanel()
        self.mod_list.mod_selected.connect(self._on_mod_selected)
        self.mod_list.active_mods_changed.connect(self._on_active_mods_changed)
        h_layout.addWidget(self.mod_list, stretch=3)

        self.mod_info = ModInfoPanel()
        self.mod_info.setMinimumWidth(300)
        h_layout.addWidget(self.mod_info, stretch=2)

        outer_layout.addWidget(content, stretch=1)
        self.setCentralWidget(outer)

        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)
        self.status_bar.showMessage("Ready")

    # ── Header ───────────────────────────────────────────────

    def _create_header(self) -> QWidget:
        header = QWidget()
        header.setObjectName("header")
        layout = QHBoxLayout(header)
        layout.setContentsMargins(16, 8, 16, 8)

        title = QLabel("PxModRim // Mod Manager")
        title.setObjectName("appTitle")
        layout.addWidget(title)

        layout.addStretch()

        sort_btn = QPushButton("Auto-sort")
        sort_btn.setObjectName("primaryAction")
        sort_btn.clicked.connect(self._auto_sort)
        layout.addWidget(sort_btn)

        save_btn = QPushButton("Save")
        save_btn.clicked.connect(self._save_mods_config)
        save_btn.setShortcut("Ctrl+S")
        layout.addWidget(save_btn)

        return header

    # ── Public ──────────────────────────────────────────────

    @asyncSlot()
    async def load_mods_async(self) -> None:
        self.status_bar.showMessage("Scanning mods...")
        await self._mod_service.discover()
        if not self._ctx.all_mods:
            self.status_bar.showMessage("No mods found")
            return

        self.mod_list.load_mods(self._ctx.all_mods, self._ctx.active_uuids)
        self._rebuild_sidebar()
        self.status_bar.showMessage(
            f"Loaded {len(self._ctx.all_mods)} mods, "
            f"{len(self._ctx.active_uuids)} active"
        )

    # ── Private slots ───────────────────────────────────────

    @asyncSlot()
    async def _open_settings(self) -> None:
        result, dialog = await await_dialog(SettingsPanel, self._ctx.config)
        if result != 1:
            return
        cfg = dialog.get_config()
        if not cfg.paths.game:
            logger.warning("Settings saved without a game path")
        save_config(cfg)
        self._ctx.update_config(cfg)
        from pxmodrim.core.providers import create_providers

        self._mod_service.reset_providers(create_providers(cfg.paths))
        await self.load_mods_async()

    def _show_about(self) -> None:
        QMessageBox.about(
            self,
            "About PxModRim",
            "PxModRim v0.1.0\nA mod manager for RimWorld.",
        )

    def _auto_sort(self) -> None:
        logger.info("Auto-sort not yet implemented")

    @asyncSlot()
    async def _save_mods_config(self) -> None:
        active_ids = self.mod_list.active_uuids()
        ok = await self._mod_service.save_active_layout(active_ids)
        if ok:
            self.status_bar.showMessage(f"Saved {len(active_ids)} active mods", 3000)
        else:
            self.status_bar.showMessage("Config folder not set", 3000)

    def _on_mod_selected(self, mod: object) -> None:
        if isinstance(mod, ListedMod):
            self.mod_info.show_mod(mod)

    def _on_active_mods_changed(self) -> None:
        uuids = self.mod_list.active_uuids()
        self._update_sidebar_counts(uuids)
        # Re-apply the current entry's filter
        selected = self.sidebar.filter_list.currentRow()
        if 0 <= selected < len(self.sidebar._entries):
            self._apply_entry(self.sidebar._entries[selected])

    def _on_entry_selected(self, entry: SidebarEntry) -> None:
        self._apply_entry(entry)

    def _apply_entry(self, entry: SidebarEntry) -> None:
        self.mod_list.show_uuids(entry.visible_uuids)

    # ── Sidebar ─────────────────────────────────────────────

    def _rebuild_sidebar(self) -> None:
        mods = self._ctx.all_mods
        active = self._ctx.active_uuids

        by_provider: dict[str, list[str]] = {}
        errors: list[str] = []
        for u, m in mods.items():
            by_provider.setdefault(m.provider_id, []).append(u)
            if not m.valid:
                errors.append(u)

        entries: list[SidebarEntry] = [
            AllModsEntry(),
            ActiveModsEntry(),
        ]
        all_uuids = set(mods.keys())
        for pid in sorted(by_provider):
            entries.append(
                ProviderModsEntry(
                    pid,
                    PROVIDER_LABELS.get(pid, pid),
                    set(by_provider[pid]),
                )
            )
        entries.append(InactiveModsEntry())
        entries.append(ErrorModsEntry())

        # Set pre-computed sets on built-in entries
        entries[0].visible_uuids = all_uuids  # All
        entries[0].refresh_count()
        entries[1].visible_uuids = set(active)  # Active
        entries[1].refresh_count()
        entries[-2].visible_uuids = all_uuids - set(active)  # Inactive
        entries[-2].refresh_count()
        entries[-1].visible_uuids = set(errors)  # Errors
        entries[-1].refresh_count()

        self.sidebar.set_entries(entries)

    def _update_sidebar_counts(self, active_uuids: list[str]) -> None:
        mods = self._ctx.all_mods
        active_set = set(active_uuids)
        for entry in self.sidebar._entries:
            entry.update_uuids(mods, active_set)
        self.sidebar.update_counts()
