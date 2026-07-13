from __future__ import annotations

from importlib.metadata import version
from importlib.resources import files as resource_files

from PySide6.QtCore import Qt
from PySide6.QtGui import QPixmap
from PySide6.QtWidgets import (
    QDialog,
    QHBoxLayout,
    QLabel,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.ui.components import AppButton


class AboutPanel(QDialog):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("aboutPanel")
        self.setWindowTitle("About PxModRim")
        self.setModal(True)
        self.resize(600, 450)

        layout = QVBoxLayout(self)

        tabs = QTabWidget()
        tabs.addTab(self._create_about_tab(), "About")
        tabs.addTab(self._create_credits_tab(), "Credits")
        layout.addWidget(tabs)

        close_btn = AppButton("Close")
        close_btn.clicked.connect(self.reject)
        layout.addWidget(close_btn, alignment=Qt.AlignmentFlag.AlignRight)

    def _create_about_tab(self) -> QWidget:
        tab = QWidget()
        header = QHBoxLayout(tab)
        header.setSpacing(20)

        left_vbox = QVBoxLayout()
        left_vbox.setAlignment(Qt.AlignmentFlag.AlignTop)

        logo_label = QLabel()
        logo_pixmap = QPixmap(str(resource_files("pxmodrim.ui.assets") / "logo.svg"))
        logo_label.setPixmap(
            logo_pixmap.scaled(
                80,
                80,
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            )
        )
        left_vbox.addWidget(logo_label, alignment=Qt.AlignmentFlag.AlignHCenter)

        name_label = QLabel("PxModRim")
        name_label.setObjectName("aboutTitle")
        name_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        left_vbox.addWidget(name_label)

        version_label = QLabel(f"v{self._get_version()}")
        version_label.setObjectName("aboutVersion")
        version_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        left_vbox.addWidget(version_label)

        header.addLayout(left_vbox)

        right_vbox = QVBoxLayout()
        right_vbox.setSpacing(12)

        desc_label = QLabel("A mod manager for RimWorld.")
        desc_label.setObjectName("aboutDescription")
        desc_label.setWordWrap(True)
        right_vbox.addWidget(desc_label)

        author_label = QLabel("Author: PyXiion")
        author_label.setObjectName("aboutMeta")
        right_vbox.addWidget(author_label)

        github_link = QLabel(
            '<a href="https://github.com/PyXiion/PxModRim" style="color: #66c0f4;">GitHub</a>'
        )
        github_link.setOpenExternalLinks(True)
        github_link.setObjectName("aboutMeta")
        right_vbox.addWidget(github_link)

        license_label = QLabel("License: MIT")
        license_label.setObjectName("aboutMeta")
        right_vbox.addWidget(license_label)

        disclaimer = QLabel(
            '<i style="color: #949ba4;">This is an unofficial fan-made tool. '
            "RimWorld is a trademark of Ludeon Studios.</i>"
        )
        disclaimer.setWordWrap(True)
        right_vbox.addWidget(disclaimer)

        right_vbox.addStretch()

        header.addLayout(right_vbox, stretch=1)

        return tab

    def _create_credits_tab(self) -> QWidget:
        tab = QWidget()
        layout = QVBoxLayout(tab)

        deps = [
            "httpx",
            "loguru",
            "lxml",
            "msgspec",
            "natsort",
            "networkx",
            "pyside6",
            "qasync",
            "toposort",
        ]

        for dep in deps:
            try:
                ver = version(dep)
                label = QLabel(f"{dep} {ver}")
                label.setObjectName("creditItem")
                layout.addWidget(label)
            except Exception:
                pass

        layout.addStretch()
        return tab

    @staticmethod
    def _get_version() -> str:
        try:
            return version("pxmodrim")
        except Exception:
            return "0.1.0"
