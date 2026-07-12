from __future__ import annotations

import asyncio
from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtGui import QImage, QPixmap
from PySide6.QtWidgets import QLabel, QScrollArea, QSizePolicy, QVBoxLayout, QWidget

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
        self._preview_task: asyncio.Task[None] | None = None

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        self._scroll = QScrollArea()
        self._scroll.setWidgetResizable(True)
        self._scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        layout.addWidget(self._scroll)

        self._content = QWidget()
        cl = QVBoxLayout(self._content)
        cl.setContentsMargins(0, 0, 0, 0)
        cl.setSpacing(0)
        self._scroll.setWidget(self._content)

        # Banner
        self._banner = AspectRatioBanner(max_height=300)
        self._banner.hide()
        cl.addWidget(self._banner, 0, Qt.AlignmentFlag.AlignTop)

        # Placeholder (shown when no mod is selected, hidden when a mod is shown)
        self._placeholder = QLabel("Select a mod to view details")
        self._placeholder.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._placeholder.setObjectName("placeholder")
        cl.addWidget(self._placeholder, stretch=1)

        # Info area
        self._info_area = QWidget()
        self._info_area.setObjectName("infoArea")
        self._info_area.setSizePolicy(
            QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Maximum
        )
        self._info_area.hide()
        ia_layout = QVBoxLayout(self._info_area)
        ia_layout.setContentsMargins(24, 24, 24, 24)
        ia_layout.setSpacing(0)
        cl.addWidget(self._info_area, 0, Qt.AlignmentFlag.AlignTop)

        # Meta grid
        self._meta_grid = ResponsiveMetaGrid(
            {
                "package_id": "Package ID",
                "author": "Author",
                "version": "Version",
                "source": "Source",
            }
        )
        ia_layout.addWidget(self._meta_grid, 0, Qt.AlignmentFlag.AlignTop)

        ia_layout.addSpacing(16)

        # Description accordion
        self._desc_renderer = DescriptionRenderer()
        self._desc_section = AccordionSection(
            "Description",
            self._desc_renderer,
            expanded=self._config.ui.desc_expanded,
        )
        self._desc_section.toggled.connect(self._on_desc_toggled)
        self._desc_section.hide()
        ia_layout.addWidget(self._desc_section, 0, Qt.AlignmentFlag.AlignTop)

        ia_layout.addSpacing(16)

        # Dependencies accordion
        self._deps_label = QLabel("None")
        self._deps_label.setWordWrap(True)
        self._deps_label.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )
        self._deps_section = AccordionSection(
            "Dependencies",
            self._deps_label,
            expanded=self._config.ui.deps_expanded,
        )
        self._deps_section.toggled.connect(self._on_deps_toggled)
        self._deps_section.hide()
        ia_layout.addWidget(self._deps_section, 0, Qt.AlignmentFlag.AlignTop)

    def show_mod(self, mod: ListedMod) -> None:
        if self._preview_task and not self._preview_task.done():
            self._preview_task.cancel()

        mod_id = getattr(mod, "package_id", None)
        if mod_id is None:
            mod_id = mod.name

        self._mod = mod
        self._current_mod_id = mod_id

        self._placeholder.hide()
        self._banner.show()
        self._info_area.show()

        self._scroll.verticalScrollBar().setValue(0)

        self._banner.setTitle(mod.name)

        self._preview_task = asyncio.ensure_future(
            self._load_preview(mod.mod_path, mod_id)
        )

        if isinstance(mod, AboutXmlMod):
            self._meta_grid.update_values(
                {
                    "package_id": str(mod.package_id),
                    "author": ", ".join(mod.authors) if mod.authors else "—",
                    "version": mod.mod_version or "—",
                    "source": mod.provider_id or "—",
                }
            )
        else:
            self._meta_grid.update_values(
                {
                    "package_id": "—",
                    "author": "—",
                    "version": "—",
                    "source": "—",
                }
            )

        # Description
        if mod.description:
            self._desc_renderer.set_description(mod.description)
            self._desc_section.show()
        else:
            self._desc_section.hide()

        # Dependencies
        if isinstance(mod, AboutXmlMod) and mod.about_rules.dependencies:
            deps = [str(d.package_id) for d in mod.about_rules.dependencies.values()]
            self._deps_label.setText("<br>".join(f"• {d}" for d in deps))
            self._deps_section.show()
        else:
            self._deps_section.hide()

    async def _load_preview(self, mod_path: Path | None, mod_id: str) -> None:
        """Load preview image in background thread. Only updates UI if still current mod."""
        try:
            preview = await asyncio.to_thread(_find_preview, mod_path)
            if preview is None:
                if self._current_mod_id == mod_id:
                    self._banner.setPixmap(QPixmap())
                return

            image = await asyncio.to_thread(QImage, str(preview))
            if self._current_mod_id != mod_id:
                return
            self._banner.setPixmap(
                QPixmap() if image.isNull() else QPixmap.fromImage(image)
            )
        except asyncio.CancelledError:
            pass

    def clear(self) -> None:
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
