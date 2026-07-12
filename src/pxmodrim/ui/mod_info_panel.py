from __future__ import annotations

import asyncio
from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtGui import QPixmap
from PySide6.QtWidgets import QLabel, QScrollArea, QVBoxLayout, QWidget

from pxmodrim._compat.config import AppConfig, save_config
from pxmodrim.models.metadata.structures import AboutXmlMod, ListedMod
from pxmodrim.ui.components import (
    AccordionSection,
    AspectRatioBanner,
    DescriptionRenderer,
    ResponsiveMetaGrid,
)


def _find_preview(mod_path: Path | None) -> Path | None:
    if mod_path is None:
        return None
    candidate = mod_path / "About" / "Preview.png"
    return candidate if candidate.exists() else None


class ModInfoPanel(QWidget):
    def __init__(self, config: AppConfig) -> None:
        super().__init__()
        self._config = config
        self._mod: ListedMod | None = None
        self._current_mod_id: str | None = None

        # Task for loading preview icons
        # We cancel it when we switch mods, so we don't have any race conditions
        self._preview_task: "asyncio.Task[None] | None" = None
        
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        layout.addWidget(scroll)
        
        self._content = QWidget()
        cl = QVBoxLayout(self._content)
        cl.setContentsMargins(0, 0, 0, 0)
        cl.setSpacing(0)
        scroll.setWidget(self._content)
        
        # Placeholder
        self._placeholder = QLabel("Select a mod to view details")
        self._placeholder.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._placeholder.setObjectName("placeholder")
        self._placeholder.setMinimumHeight(200)
        cl.addWidget(self._placeholder, stretch=1)
        
        # Banner
        self._banner = AspectRatioBanner(max_height=300)
        self._banner.hide()
        cl.addWidget(self._banner)
        
        # Info area
        self._info_area = QWidget()
        self._info_area.hide()
        ia_layout = QVBoxLayout(self._info_area)
        ia_layout.setContentsMargins(16, 16, 16, 16)
        ia_layout.setSpacing(16)
        cl.addWidget(self._info_area)
        
        # Meta grid
        self._meta_grid = ResponsiveMetaGrid({
            "package_id": "Package ID",
            "author": "Author",
            "version": "Version",
            "source": "Source",
        })
        ia_layout.addWidget(self._meta_grid)
        
        # Dependencies accordion
        self._deps_label = QLabel("None")
        self._deps_label.setWordWrap(True)
        self._deps_label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        self._deps_section = AccordionSection(
            "Dependencies",
            self._deps_label,
            expanded=self._config.ui.deps_expanded,
        )
        self._deps_section.toggled.connect(self._on_deps_toggled)
        self._deps_section.hide()
        ia_layout.addWidget(self._deps_section)
        
        # Description accordion
        self._desc_renderer = DescriptionRenderer()
        self._desc_section = AccordionSection(
            "Description",
            self._desc_renderer,
            expanded=self._config.ui.desc_expanded,
        )
        self._desc_section.toggled.connect(self._on_desc_toggled)
        self._desc_section.hide()
        ia_layout.addWidget(self._desc_section)
        
        ia_layout.addStretch()
    
    def show_mod(self, mod: ListedMod) -> None:
        # Cancel any pending preview task
        if self._preview_task and not self._preview_task.done():
            self._preview_task.cancel()
        
        # Use package_id as unique identifier for the mod
        mod_id = getattr(mod, 'package_id', None)
        if mod_id is None:
            mod_id = mod.name
        
        self._mod = mod
        self._current_mod_id = mod_id
        
        self._placeholder.hide()
        self._banner.show()
        self._info_area.show()
        
        self._banner.setTitle(mod.name)
        
        # Start async preview load with mod_id tracking
        self._preview_task = asyncio.create_task(self._load_preview_async(mod.mod_path, mod_id))
        
        # Meta (sync, fast)
        if isinstance(mod, AboutXmlMod):
            self._meta_grid.update_values({
                "package_id": str(mod.package_id),
                "author": ", ".join(mod.authors) if mod.authors else "—",
                "version": mod.mod_version or "—",
                "source": mod.provider_id or "—",
            })
        else:
            self._meta_grid.update_values({
                "package_id": "—",
                "author": "—",
                "version": "—",
                "source": "—",
            })
        
        # Dependencies
        if isinstance(mod, AboutXmlMod) and mod.about_rules.dependencies:
            deps = [str(d.package_id) for d in mod.about_rules.dependencies.values()]
            self._deps_label.setText("<br>".join(f"• {d}" for d in deps))
            self._deps_section.show()
        else:
            self._deps_section.hide()
        
        # Description
        if mod.description:
            self._desc_renderer.set_description(mod.description)
            self._desc_section.show()
        else:
            self._desc_section.hide()
    
    async def _load_preview_async(self, mod_path: Path | None, mod_id: str) -> None:
        """Load preview image in background thread. Only updates UI if still current mod."""
        try:
            preview = _find_preview(mod_path)
            if preview:
                pixmap = await asyncio.to_thread(QPixmap, str(preview))
                # Only update if this is still the current mod
                if self._current_mod_id == mod_id and not pixmap.isNull():
                    self._banner.setPixmap(pixmap)
            else:
                if self._current_mod_id == mod_id:
                    self._banner.setPixmap(QPixmap())
        except asyncio.CancelledError:
            # Task was cancelled, ignore
            pass
    
    def clear(self) -> None:
        if self._preview_task and not self._preview_task.done():
            self._preview_task.cancel()
        self._mod = None
        self._current_mod_id = None
        self._placeholder.show()
        self._banner.hide()
        self._info_area.hide()
    
    def _on_deps_toggled(self, expanded: bool) -> None:
        self._config.ui.deps_expanded = expanded
        save_config(self._config)
    
    def _on_desc_toggled(self, expanded: bool) -> None:
        self._config.ui.desc_expanded = expanded
        save_config(self._config)