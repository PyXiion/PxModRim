from __future__ import annotations

from typing import TYPE_CHECKING

from PySide6.QtCore import QUrl
from PySide6.QtGui import QDesktopServices
from PySide6.QtWidgets import (
    QDialog,
    QHBoxLayout,
    QLabel,
    QTextBrowser,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.ui.components.button import AppButton

if TYPE_CHECKING:
    from pxmodrim.core.services.update_service import ReleaseInfo

SKIP_RESULT = 2

_AUTO_UPDATE_NOTE = (
    "Automatic updating may be added later. For now, downloading the release "
    "yourself is the safest way to avoid update bugs."
)

_WEB_SCHEMES = frozenset({"http", "https"})


def _open_web_link(url: QUrl) -> None:
    if url.scheme().lower() in _WEB_SCHEMES:
        QDesktopServices.openUrl(url)


class UpdateDialog(QDialog):
    """Offers a newer release. Accepted = open release page, SKIP_RESULT = skip it."""

    def __init__(
        self,
        release: ReleaseInfo,
        current_version: str,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.setWindowTitle("Update Available")
        self.resize(560, 460)
        layout = QVBoxLayout(self)

        title = QLabel(f"PxModRim {release.tag} is available", self)
        title.setStyleSheet("font-size: 16px; font-weight: 600;")
        layout.addWidget(title)
        layout.addWidget(QLabel(f"You are running {current_version}.", self))

        notes = QTextBrowser(self)
        notes.setOpenLinks(False)
        notes.setOpenExternalLinks(False)
        notes.anchorClicked.connect(_open_web_link)
        notes.setMarkdown(release.notes or "No release notes.")
        layout.addWidget(notes, 1)

        note = QLabel(_AUTO_UPDATE_NOTE, self)
        note.setWordWrap(True)
        layout.addWidget(note)

        buttons = QHBoxLayout()
        skip_btn = AppButton("Skip This Version", self)
        skip_btn.clicked.connect(lambda: self.done(SKIP_RESULT))
        later_btn = AppButton("Later", self)
        later_btn.clicked.connect(self.reject)
        download_btn = AppButton("Download", self)
        download_btn.setObjectName("primaryAction")
        download_btn.setDefault(True)
        download_btn.clicked.connect(self.accept)
        buttons.addWidget(skip_btn)
        buttons.addStretch()
        buttons.addWidget(later_btn)
        buttons.addWidget(download_btn)
        layout.addLayout(buttons)
