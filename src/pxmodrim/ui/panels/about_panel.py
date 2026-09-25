from __future__ import annotations

import platform
import sys
from importlib.metadata import PackageNotFoundError, version
from importlib.resources import files as resource_files

from PySide6.QtCore import Qt, QUrl, qVersion
from PySide6.QtGui import QDesktopServices, QPixmap
from PySide6.QtWidgets import (
    QApplication,
    QDialog,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QTextBrowser,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.ui.components import AppButton


class AboutPanel(QDialog):
    _REPOSITORY_URL = "https://github.com/PyXiion/PxModRim"
    _ISSUES_URL = f"{_REPOSITORY_URL}/issues"
    _DEPENDENCIES = (
        ("aiosqlite", "MIT"),
        ("httpx", "BSD-3-Clause"),
        ("loguru", "MIT"),
        ("lxml", "BSD-3-Clause"),
        ("msgspec", "BSD-3-Clause"),
        ("PySide6", "LGPL-3.0 / GPL-2.0 / GPL-3.0"),
        ("qasync", "BSD-2-Clause"),
        ("toposort", "Apache-2.0"),
        ("ttimer", "MIT"),
    )

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("aboutPanel")
        self.setWindowTitle("About PxModRim")
        self.setModal(True)
        self.resize(700, 620)
        self.setMinimumSize(640, 560)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(24, 24, 24, 20)
        layout.setSpacing(18)
        layout.addWidget(self._create_hero())
        layout.addWidget(self._create_project_story())
        layout.addWidget(self._create_details())
        self._credits_dialog = self._create_credits_dialog()
        layout.addLayout(self._create_actions())

        disclaimer = QLabel(
            "PxModRim is an unofficial fan-made tool. RimWorld is a trademark "
            "of Ludeon Studios.",
            self,
        )
        disclaimer.setObjectName("aboutDisclaimer")
        disclaimer.setWordWrap(True)
        layout.addWidget(disclaimer)

        footer = QHBoxLayout()
        footer.addStretch()
        close_button = AppButton("Close", self)
        close_button.clicked.connect(self.reject)
        footer.addWidget(close_button)
        layout.addLayout(footer)

    def _create_hero(self) -> QFrame:
        hero = QFrame(self)
        hero.setObjectName("aboutHero")
        layout = QHBoxLayout(hero)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(20)

        logo = QLabel(hero)
        pixmap = QPixmap(str(resource_files("pxmodrim.ui.assets") / "logo.svg"))
        logo.setPixmap(
            pixmap.scaled(
                96,
                96,
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            )
        )
        layout.addWidget(logo, alignment=Qt.AlignmentFlag.AlignTop)

        identity = QVBoxLayout()
        identity.setSpacing(6)

        title_row = QHBoxLayout()
        title = QLabel("PxModRim", hero)
        title.setObjectName("aboutTitle")
        title_row.addWidget(title)
        title_row.addStretch()

        version_badge = QLabel(f"v{self._get_version()}", hero)
        version_badge.setObjectName("aboutVersionBadge")
        title_row.addWidget(version_badge, alignment=Qt.AlignmentFlag.AlignVCenter)
        identity.addLayout(title_row)

        tagline = QLabel("A friendly, modern mod manager for RimWorld.", hero)
        tagline.setObjectName("aboutTagline")
        identity.addWidget(tagline)

        description = QLabel(
            "Scan, sort, resolve dependencies, and manage large mod lists easily.",
            hero,
        )
        description.setObjectName("aboutDescription")
        description.setWordWrap(True)
        identity.addWidget(description)
        identity.addStretch()

        layout.addLayout(identity, stretch=1)
        return hero

    def _create_project_story(self) -> QWidget:
        section = QWidget(self)
        layout = QVBoxLayout(section)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(6)

        heading = QLabel("THE PROJECT", section)
        heading.setObjectName("aboutSectionTitle")
        layout.addWidget(heading)

        story = QLabel(
            "I used RimPy and RimSort for a long time myself, but with recent "
            "updates it kept getting slower and worse — some features even broke "
            "my game 🙁<br><br>"
            "I initially started working on a PR to fix most of the parallelism "
            "issues in RimSort, but realized it didn't make much sense. So I decided "
            "to try creating my own alternative with the help of AI. The result "
            "is what you see on your screen.<br><br>"
            "This is all just my personal opinion based on my experience with "
            "RimSort at the time, and things may have changed for the better since "
            "then (I hope so!).<br><br>"
            "<b>— PyXiion</b>",
            section,
        )
        story.setObjectName("aboutStory")
        story.setWordWrap(True)
        layout.addWidget(story)
        return section

    def _create_details(self) -> QFrame:
        details = QFrame(self)
        details.setObjectName("aboutDetails")
        layout = QGridLayout(details)
        layout.setContentsMargins(16, 14, 16, 14)
        layout.setHorizontalSpacing(14)
        layout.setVerticalSpacing(8)

        python_version = f"{sys.version_info.major}.{sys.version_info.minor}"
        environment = (
            f"Python {python_version}  •  Qt {qVersion()}  •  {platform.system()}"
        )
        rows = (
            ("CREATED BY", "PyXiion"),
            ("LICENSE", "LGPL-3.0"),
            ("INSPIRED BY", "RimSort"),
            ("ENVIRONMENT", environment),
        )
        for row, (label_text, value_text) in enumerate(rows):
            label = QLabel(label_text, details)
            label.setObjectName("aboutMetaLabel")
            layout.addWidget(label, row, 0)

            value = QLabel(value_text, details)
            value.setObjectName("aboutMetaValue")
            layout.addWidget(value, row, 1)

        layout.setColumnStretch(1, 1)
        return details

    def _create_actions(self) -> QHBoxLayout:
        actions = QHBoxLayout()
        actions.setSpacing(8)

        github_button = AppButton("GitHub", self)
        github_button.setObjectName("primaryAction")
        github_button.clicked.connect(
            lambda: QDesktopServices.openUrl(QUrl(self._REPOSITORY_URL))
        )
        actions.addWidget(github_button)

        issues_button = AppButton("Report issue", self)
        issues_button.clicked.connect(
            lambda: QDesktopServices.openUrl(QUrl(self._ISSUES_URL))
        )
        actions.addWidget(issues_button)

        self._copy_button = AppButton("Copy system info", self)
        self._copy_button.clicked.connect(self._copy_system_information)
        actions.addWidget(self._copy_button)

        credits_button = AppButton("Open-source credits…", self)
        credits_button.clicked.connect(self._credits_dialog.open)
        actions.addWidget(credits_button)
        actions.addStretch()
        return actions

    def _create_credits_dialog(self) -> QDialog:
        dialog = QDialog(self)
        dialog.setObjectName("creditsDialog")
        dialog.setWindowTitle("Open-source credits")
        dialog.setModal(True)
        dialog.resize(560, 500)

        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(20, 20, 20, 16)
        layout.setSpacing(14)

        title = QLabel("Open-source credits", dialog)
        title.setObjectName("creditsTitle")
        layout.addWidget(title)

        credits = QTextBrowser(dialog)
        credits.setObjectName("creditsBrowser")
        credits.setOpenExternalLinks(True)
        credits.setHtml(self._credits_html())
        layout.addWidget(credits)

        footer = QHBoxLayout()
        footer.addStretch()
        close_button = AppButton("Close", dialog)
        close_button.clicked.connect(dialog.reject)
        footer.addWidget(close_button)
        layout.addLayout(footer)
        return dialog

    def _credits_html(self) -> str:
        dependencies = "".join(
            "<tr>"
            f'<td><a href="https://pypi.org/project/{name}/">{name}</a></td>'
            f"<td>{version(name)}</td>"
            f"<td>{license_name}</td>"
            "</tr>"
            for name, license_name in self._DEPENDENCIES
        )
        return f"""
            <h2>PxModRim</h2>
            <p>Created and maintained by <b>PyXiion</b>.</p>
            <h2>RimSort</h2>
            <p>
                Portions are derived from
                <a href="https://github.com/RimSort/RimSort">RimSort</a>
                and used under the MIT license.
            </p>
            <h2>Runtime dependencies</h2>
            <table width="100%" cellspacing="0" cellpadding="5">
                <tr>
                    <th align="left">Package</th>
                    <th align="left">Version</th>
                    <th align="left">License</th>
                </tr>
                {dependencies}
            </table>
        """

    def _copy_system_information(self) -> None:
        QApplication.clipboard().setText(self._system_information())
        self._copy_button.setText("Copied")

    def _system_information(self) -> str:
        return "\n".join(
            (
                f"PxModRim {self._get_version()}",
                f"Python {platform.python_version()}",
                f"PySide6 {version('PySide6')}",
                f"Qt {qVersion()}",
                (
                    f"Operating system: {platform.system()} {platform.release()} "
                    f"({platform.machine()})"
                ),
            )
        )

    @staticmethod
    def _get_version() -> str:
        try:
            return version("pxmodrim")
        except PackageNotFoundError:
            return "0.1.0"
