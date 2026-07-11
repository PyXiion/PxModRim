from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtGui import QPixmap
from PySide6.QtWidgets import (
    QGridLayout,
    QLabel,
    QScrollArea,
    QTextBrowser,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.models.metadata.structures import AboutXmlMod, ListedMod

BANNER_HEIGHT = 160


class ModInfoPanel(QWidget):
    def __init__(self) -> None:
        super().__init__()
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
        cl.addWidget(self._placeholder)

        # Banner
        self._banner = QLabel()
        self._banner.setObjectName("modBanner")
        self._banner.setFixedHeight(BANNER_HEIGHT)
        self._banner.setAlignment(
            Qt.AlignmentFlag.AlignBottom | Qt.AlignmentFlag.AlignLeading
        )
        self._banner.hide()
        cl.addWidget(self._banner)

        # Content area
        self._info_area = QWidget()
        self._info_area.setObjectName("infoArea")
        ia_layout = QVBoxLayout(self._info_area)
        ia_layout.setContentsMargins(12, 12, 12, 12)
        ia_layout.setSpacing(16)
        self._info_area.hide()
        cl.addWidget(self._info_area)

        # Title (shown below banner)
        self._title_label = QLabel()
        self._title_label.setObjectName("bannerTitle")
        self._title_label.setWordWrap(True)
        self._title_label.hide()
        ia_layout.addWidget(self._title_label)

        # Meta grid
        self._meta_widget = QWidget()
        self._meta_widget.setObjectName("metaWidget")
        mw_layout = QVBoxLayout(self._meta_widget)
        mw_layout.setContentsMargins(8, 8, 8, 8)

        self._meta_grid = QGridLayout()
        self._meta_grid.setSpacing(8)
        mw_layout.addLayout(self._meta_grid)
        ia_layout.addWidget(self._meta_widget)

        self._meta_fields: dict[str, QLabel] = {}
        for label, key in [
            ("PackageID", "package_id"),
            ("Author", "author"),
            ("Version", "version"),
            ("Source", "source"),
        ]:
            container = QWidget()
            c_layout = QVBoxLayout(container)
            c_layout.setContentsMargins(0, 0, 0, 0)
            c_layout.setSpacing(2)
            lbl = QLabel(label)
            lbl.setObjectName("metaLabel")
            val = QLabel("\u2014")
            val.setObjectName("metaValue")
            val.setWordWrap(True)
            c_layout.addWidget(lbl)
            c_layout.addWidget(val)
            self._meta_fields[key] = val
            row = 0 if key in ("package_id", "author") else 1
            col = 0 if key in ("package_id", "version") else 1
            self._meta_grid.addWidget(container, row, col)

        # Dependencies
        self._deps_section = QWidget()
        self._deps_section.setObjectName("depsSection")
        deps_layout = QVBoxLayout(self._deps_section)
        deps_layout.setContentsMargins(0, 0, 0, 0)
        deps_layout.setSpacing(4)
        deps_title = QLabel("Dependencies")
        deps_title.setObjectName("sectionTitle")
        deps_layout.addWidget(deps_title)
        self._deps_list = QLabel("None")
        self._deps_list.setWordWrap(True)
        self._deps_list.setObjectName("depList")
        deps_layout.addWidget(self._deps_list)
        self._deps_section.hide()
        ia_layout.addWidget(self._deps_section)

        # Description
        self._desc_browser = QTextBrowser()
        self._desc_browser.setObjectName("descriptionBrowser")
        self._desc_browser.setOpenExternalLinks(True)
        self._desc_browser.setMinimumHeight(80)
        self._desc_browser.hide()
        ia_layout.addWidget(self._desc_browser)

        self._content_layout = cl
        self._info_layout = ia_layout

    def show_mod(self, mod: ListedMod) -> None:
        self._placeholder.hide()
        self._banner.show()
        self._info_area.show()

        self._banner.setText(mod.name)
        self._banner.setPixmap(QPixmap())
        self._title_label.setText(mod.name)

        preview = _find_preview(mod.mod_path)
        if preview:
            pixmap = QPixmap(str(preview))
            if not pixmap.isNull():
                scaled = pixmap.scaledToWidth(
                    self.width(), Qt.TransformationMode.SmoothTransformation
                )
                self._banner.setPixmap(scaled)

        # Meta
        self._meta_fields["package_id"].setText(
            str(mod.package_id) if isinstance(mod, AboutXmlMod) else "\u2014"
        )
        self._meta_fields["author"].setText(
            ", ".join(mod.authors)
            if isinstance(mod, AboutXmlMod) and mod.authors
            else "\u2014"
        )
        self._meta_fields["version"].setText(
            mod.mod_version
            if isinstance(mod, AboutXmlMod) and mod.mod_version
            else "\u2014"
        )
        self._meta_fields["source"].setText(
            mod.provider_id if mod.provider_id else "\u2014"
        )

        # Dependencies
        if isinstance(mod, AboutXmlMod) and mod.about_rules.dependencies:
            deps: list[str] = [
                str(dep_mod.package_id)
                for dep_mod in mod.about_rules.dependencies.values()
            ]
            self._deps_list.setText("<br>".join(f"<b>{d}</b>" for d in deps))
            self._deps_section.show()
        else:
            self._deps_section.hide()

        # Description
        if mod.description:
            escaped = (
                mod.description.replace("&", "&amp;")
                .replace("<", "&lt;")
                .replace(">", "&gt;")
            )
            html = escaped.replace("\n", "<br>")
            self._desc_browser.setHtml(
                f'<div style="font-size:13px;color:#cdd2d6;line-height:1.5;">{html}</div>'
            )
            self._desc_browser.show()
        else:
            self._desc_browser.hide()

    def clear(self) -> None:
        self._placeholder.show()
        self._banner.hide()
        self._info_area.hide()


def _find_preview(mod_path: Path | None) -> Path | None:
    if mod_path is None:
        return None
    candidate = mod_path / "About" / "Preview.png"
    return candidate if candidate.exists() else None
