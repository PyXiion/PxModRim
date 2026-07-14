from __future__ import annotations

import asyncio
from pathlib import Path
from typing import TYPE_CHECKING

from PySide6.QtCore import Qt, QUrl
from PySide6.QtGui import QDesktopServices, QImage, QPixmap
from PySide6.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QPushButton,
    QScrollArea,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.core.config import AppConfig, save_config
from pxmodrim.core.models.metadata.structures import AboutXmlMod, ListedMod
from pxmodrim.ui.components import (
    AccordionSection,
    AspectRatioBanner,
    DescriptionRenderer,
    MetaChipRow,
    generate_preview,
)
from pxmodrim.ui.components.icons import icon, pixmap
from pxmodrim.ui.theme.palette import PALETTE

if TYPE_CHECKING:
    from pxmodrim.core.models.view.diagnostics import ModIssueView


def _first_sentence(text: str, max_len: int = 80) -> str:
    if not text:
        return ""
    stripped = text.strip()
    end = stripped.find(".")
    result = stripped[:end + 1] if end != -1 else stripped
    if len(result) > max_len:
        result = result[:max_len].rsplit(" ", 1)[0] + "\u2026"
    return result


def _find_preview(mod_path: Path | None) -> Path | None:
    if mod_path is None:
        return None
    candidate = mod_path / "About" / "Preview.png"
    return candidate if candidate.exists() else None


class IssueRow(QWidget):
    def __init__(self, issue: ModIssueView, parent: QWidget | None = None) -> None:
        super().__init__(parent)

        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 4, 0, 4)
        layout.setSpacing(8)

        icon_name = "error" if issue.is_error else "warning"
        color = "#ed4245" if issue.is_error else "#f9a825"
        icon_label = QLabel()
        icon_label.setPixmap(pixmap(icon_name, 16, color))
        icon_label.setFixedSize(16, 16)
        layout.addWidget(icon_label, 0, Qt.AlignmentFlag.AlignTop)

        content = QVBoxLayout()
        content.setContentsMargins(0, 0, 0, 0)
        content.setSpacing(2)

        cat_label = QLabel(issue.category_display_name)
        cat_label.setStyleSheet(f"color: {color}; font-weight: bold;")
        content.addWidget(cat_label)

        if issue.detail:
            detail_label = QLabel(issue.detail)
            detail_label.setWordWrap(True)
            detail_label.setStyleSheet("color: #949ba4;")
            content.addWidget(detail_label)

        layout.addLayout(content, 1)


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
        self._banner = AspectRatioBanner(self, max_height=260)
        self._banner.hide()
        cl.addWidget(self._banner, 0, Qt.AlignmentFlag.AlignTop)

        # Placeholder (shown when no mod is selected)
        self._placeholder = QLabel("Select a mod to view details")
        self._placeholder.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._placeholder.setObjectName("placeholder")
        cl.addWidget(self._placeholder, 0, Qt.AlignmentFlag.AlignTop)

        # Info area
        self._info_area = QWidget()
        self._info_area.setObjectName("infoArea")
        self._info_area.hide()
        ia_layout = QVBoxLayout(self._info_area)
        ia_layout.setContentsMargins(0, 0, 0, 0)
        ia_layout.setSpacing(0)
        cl.addWidget(self._info_area, stretch=1)

        # Open mod folder button
        self._btn_container = QWidget()
        self._btn_container.setObjectName("infoButtonContainer")
        btn_layout = QHBoxLayout(self._btn_container)
        btn_layout.setContentsMargins(16, 8, 16, 8)

        self._open_folder_btn = QPushButton()
        self._open_folder_btn.setIcon(icon("folder", 16, "#949ba4"))
        self._open_folder_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self._open_folder_btn.setToolTip("Open mod folder")
        self._open_folder_btn.clicked.connect(self._on_open_folder)
        self._open_folder_btn.setStyleSheet(f"""
            QPushButton {{
                min-width: 32px; max-width: 32px;
                min-height: 32px; max-height: 32px;
                background-color: {PALETTE["ELEVATE_3"]};
                border: 1px solid {PALETTE["BORDER"]};
                border-radius: 6px;
                padding: 0;
            }}
            QPushButton:hover {{
                background-color: {PALETTE["ELEVATE_4"]};
                border-color: {PALETTE["ELEVATE_4"]};
            }}
        """)
        btn_layout.addWidget(self._open_folder_btn)

        self._open_url_btn = QPushButton()
        self._open_url_btn.setIcon(icon("link", 16, "#949ba4"))
        self._open_url_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self._open_url_btn.setToolTip("Open mod URL")
        self._open_url_btn.clicked.connect(self._on_open_url)
        self._open_url_btn.setStyleSheet(f"""
            QPushButton {{
                min-width: 32px; max-width: 32px;
                min-height: 32px; max-height: 32px;
                background-color: {PALETTE["ELEVATE_3"]};
                border: 1px solid {PALETTE["BORDER"]};
                border-radius: 6px;
                padding: 0;
            }}
            QPushButton:hover {{
                background-color: {PALETTE["ELEVATE_4"]};
                border-color: {PALETTE["ELEVATE_4"]};
            }}
        """)
        btn_layout.addWidget(self._open_url_btn)
        btn_layout.addStretch()

        self._btn_container.hide()
        ia_layout.addWidget(self._btn_container, 0, Qt.AlignmentFlag.AlignTop)

        # Meta chips
        self._meta_chips = MetaChipRow(
            {
                "package_id": "Package ID",
                "author": "Author",
                "version": "Version",
                "source": "Source",
            }
        )
        ia_layout.addWidget(self._meta_chips, 0, Qt.AlignmentFlag.AlignTop)

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
        self._no_issues_label = QLabel("No issues detected")
        self._no_issues_label.setObjectName("issuesLabel")
        self._no_issues_label.setWordWrap(True)
        self._no_issues_label.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )

        self._issues_container = QWidget()
        self._issues_layout = QVBoxLayout(self._issues_container)
        self._issues_layout.setContentsMargins(0, 0, 0, 0)
        self._issues_layout.setSpacing(0)
        self._issues_layout.addWidget(self._no_issues_label)

        self._issues_section = AccordionSection(
            "Issues",
            self._issues_container,
            expanded=True,
        )
        self._issues_section.hide()
        ia_layout.addWidget(self._issues_section, 0, Qt.AlignmentFlag.AlignTop)
        ia_layout.addStretch()

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

        self._btn_container.setVisible(
            mod.mod_path is not None and mod.mod_path.exists()
        )

        if isinstance(mod, AboutXmlMod) and bool(mod.url):
            self._open_url_btn.setVisible(True)
            self._open_url_btn.setToolTip(f"Open URL: {mod.url}")
        else:
            self._open_url_btn.setVisible(False)

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
        while self._issues_layout.count():
            item = self._issues_layout.takeAt(0)
            if item is not None:
                w = item.widget()
                if w is not None and w is not self._no_issues_label:
                    w.deleteLater()

        if not issues:
            self._no_issues_label.show()
            self._issues_layout.addWidget(self._no_issues_label)
            self._issues_section.show()
            return

        self._no_issues_label.hide()
        for issue in issues:
            self._issues_layout.addWidget(IssueRow(issue))
        self._issues_section.show()

    def _on_open_folder(self) -> None:
        if self._mod is not None and self._mod.mod_path is not None:
            QDesktopServices.openUrl(QUrl.fromLocalFile(str(self._mod.mod_path)))

    def _on_open_url(self) -> None:
        if isinstance(self._mod, AboutXmlMod) and self._mod.url:
            QDesktopServices.openUrl(QUrl(self._mod.url))

    async def _load_preview(
        self, mod_path: Path | None, mod_id: str, mod_name: str
    ) -> None:
        """Load preview image in background thread. Only updates UI if current mod."""
        try:
            preview_path = await asyncio.to_thread(_find_preview, mod_path)

            if preview_path is not None:
                image = await asyncio.to_thread(QImage, str(preview_path))
                if self._current_mod_id == mod_id and not image.isNull():
                    self._banner.setPixmap(QPixmap.fromImage(image))
                    self._banner.setShowOverlay(False)
                    return

            # No usable preview — show fallback with overlay
            if self._current_mod_id == mod_id:
                w = self._banner.width() or 300
                h = self._banner.sizeHint().height()
                fallback = await asyncio.to_thread(generate_preview, mod_name, w, h)
                if self._current_mod_id == mod_id:
                    self._banner.setShowOverlay(True)
                    self._banner.setPixmap(fallback)
        except asyncio.CancelledError:
            pass

    def clear(self) -> None:
        self._mod = None
        self._current_mod_id = None
        self._placeholder.show()
        self._banner.hide()
        self._btn_container.hide()
        self._info_area.hide()

    def _on_deps_toggled(self, expanded: bool) -> None:
        self._config.ui.deps_expanded = expanded
        save_config(self._config)

    def _on_desc_toggled(self, expanded: bool) -> None:
        self._config.ui.desc_expanded = expanded
        save_config(self._config)
