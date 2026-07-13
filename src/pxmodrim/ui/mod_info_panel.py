from __future__ import annotations

import asyncio
from pathlib import Path
from typing import TYPE_CHECKING

from PySide6.QtCore import Qt
from PySide6.QtGui import QImage, QPixmap
from PySide6.QtWidgets import QLabel, QScrollArea, QVBoxLayout, QWidget

from pxmodrim._compat.config import AppConfig, save_config
from pxmodrim.models.metadata.structures import AboutXmlMod, ListedMod
from pxmodrim.ui.components import (
    AccordionSection,
    AspectRatioBanner,
    DescriptionRenderer,
    MetaChipRow,
    generate_preview,
)

if TYPE_CHECKING:
    from pxmodrim.models.view.diagnostics import ModIssueView


def _first_sentence(text: str, max_len: int = 80) -> str:
    if not text:
        return ""
    stripped = text.strip()
    end = stripped.find(".")
    if end != -1:
        result = stripped[: end + 1]
    else:
        result = stripped
    if len(result) > max_len:
        result = result[:max_len].rsplit(" ", 1)[0] + "\u2026"
    return result


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
        self._banner = AspectRatioBanner(self, max_height=200)
        self._banner.hide()
        cl.addWidget(self._banner, 0, Qt.AlignmentFlag.AlignTop)

        # Placeholder (shown when no mod is selected)
        self._placeholder = QLabel("Select a mod to view details")
        self._placeholder.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._placeholder.setObjectName("placeholder")
        cl.addWidget(self._placeholder, stretch=1)

        # Info area
        self._info_area = QWidget()
        self._info_area.setObjectName("infoArea")
        self._info_area.hide()
        ia_layout = QVBoxLayout(self._info_area)
        ia_layout.setContentsMargins(0, 0, 0, 0)
        ia_layout.setSpacing(0)
        cl.addWidget(self._info_area)

        # Meta chips
        self._meta_chips = MetaChipRow(
            {
                "package_id": "Package ID",
                "author": "Author",
                "version": "Version",
                "source": "Source",
            }
        )
        ia_layout.addWidget(self._meta_chips)

        # Description accordion
        self._desc_renderer = DescriptionRenderer()
        self._desc_section = AccordionSection(
            "Description",
            self._desc_renderer,
            expanded=self._config.ui.desc_expanded,
        )
        self._desc_section.toggled.connect(self._on_desc_toggled)
        self._desc_section.hide()
        ia_layout.addWidget(self._desc_section, 1)

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

        # Issues accordion
        self._issues_label = QLabel("No issues detected")
        self._issues_label.setObjectName("issuesLabel")
        self._issues_label.setWordWrap(True)
        self._issues_label.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )
        self._issues_section = AccordionSection(
            "Issues",
            self._issues_label,
            expanded=True,
        )
        self._issues_section.hide()
        ia_layout.addWidget(self._issues_section, 0, Qt.AlignmentFlag.AlignTop)

        self._current_issues: list[ModIssueView] = []

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
        self._banner.setSubtitle(
            _first_sentence(mod.description)
            if mod.description
            else (str(mod.package_id) if isinstance(mod, AboutXmlMod) else "")
        )

        self._preview_task = asyncio.ensure_future(
            self._load_preview(mod.mod_path, mod_id, mod.name)
        )

        if isinstance(mod, AboutXmlMod):
            self._meta_chips.update_values(
                {
                    "package_id": str(mod.package_id),
                    "author": ", ".join(mod.authors) if mod.authors else "—",
                    "version": mod.mod_version or "—",
                    "source": mod.provider_id or "—",
                }
            )
        else:
            self._meta_chips.update_values(
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

        # Issues — hidden until set_issues is called
        self._issues_section.hide()

    def set_issues(self, issues: list[ModIssueView]) -> None:
        self._current_issues = issues
        self._show_issues(issues)

    def _show_issues(self, issues: list[ModIssueView]) -> None:
        if not issues:
            self._issues_label.setText("No issues detected")
            self._issues_section.show()
            return

        parts: list[str] = []
        for issue in issues:
            icon = "\u2716" if issue.is_error else "\u26a0"
            color = "#ed4245" if issue.is_error else "#f9a825"
            parts.append(
                f'<span style="color: {color};">{icon} {issue.category}</span>'
            )
            if issue.detail:
                parts.append(f"&nbsp;&nbsp;{issue.detail}")

        self._issues_label.setText("<br>".join(parts))
        self._issues_section.show()

    async def _load_preview(
        self, mod_path: Path | None, mod_id: str, mod_name: str
    ) -> None:
        """Load preview image in background thread. Only updates UI if still current mod."""
        try:
            preview = await asyncio.to_thread(_find_preview, mod_path)
            if preview is None:
                if self._current_mod_id == mod_id:
                    w = self._banner.width() or 300
                    h = self._banner.sizeHint().height()
                    fallback = await asyncio.to_thread(generate_preview, mod_name, w, h)
                    if self._current_mod_id == mod_id:
                        self._banner.setPixmap(fallback)
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
