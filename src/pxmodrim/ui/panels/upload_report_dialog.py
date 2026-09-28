from __future__ import annotations

import asyncio
from pathlib import Path
from typing import TYPE_CHECKING, Any

from PySide6.QtGui import QGuiApplication
from PySide6.QtWidgets import (
    QDialog,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.core.support.report import (
    PasteUploader,
    ReportUploadError,
    build_report,
)
from pxmodrim.ui.components.button import AppButton
from pxmodrim.ui.components.dialogs import await_dialog

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext
    from pxmodrim.ui.components.toast import ToastManager

_DEFAULT_ENDPOINT = "https://paste.rs/"


class UploadConfirmDialog(QDialog):
    """Confirmation dialog explaining what will be uploaded and anonymized.

    Finishes with Accepted for upload, SAVE_TO_FILE for saving, Rejected to cancel.
    """

    SAVE_TO_FILE = 2

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("uploadConfirmDialog")
        self.setWindowTitle("Upload Log & System Info")
        self.setModal(True)
        self.resize(520, 320)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(16)

        title_label = QLabel("Upload Log & System Diagnostics", self)
        font = title_label.font()
        font.setBold(True)
        font.setPointSize(font.pointSize() + 2)
        title_label.setFont(font)
        layout.addWidget(title_label)

        description_text = (
            "This action generates and uploads an anonymized diagnostic report "
            "to help diagnose issues.\n\n"
            "The report includes:\n"
            "  • Recent application log contents (last ~2 MB)\n"
            "  • System details (OS, architecture, Python, Qt, PySide6)\n"
            "  • RimWorld version and configured directory paths\n"
            "  • Mod collection count and active mod list in load order\n\n"
            "Privacy & Security:\n"
            "  • Your home directory path is replaced with '~'\n"
            "  • Your username in other user-directory paths is replaced "
            "with '<user>'\n\n"
            "Notice: The uploaded report will be publicly viewable "
            "by anyone with the link."
        )
        desc_label = QLabel(description_text, self)
        desc_label.setWordWrap(True)
        layout.addWidget(desc_label)

        layout.addStretch()

        button_row = QHBoxLayout()
        button_row.setSpacing(10)

        self._save_file_btn = AppButton("Save to file instead…", self)
        self._save_file_btn.clicked.connect(lambda: self.done(self.SAVE_TO_FILE))
        button_row.addWidget(self._save_file_btn)

        button_row.addStretch()

        cancel_btn = AppButton("Cancel", self)
        cancel_btn.clicked.connect(self.reject)
        button_row.addWidget(cancel_btn)

        self._upload_btn = AppButton("Upload", self)
        self._upload_btn.setObjectName("primaryAction")
        self._upload_btn.clicked.connect(self.accept)
        button_row.addWidget(self._upload_btn)

        layout.addLayout(button_row)


class UploadFailedDialog(QDialog):
    """Error dialog shown on upload failure; Accepted means save to a file."""

    def __init__(self, error_message: str, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("uploadFailedDialog")
        self.setWindowTitle("Upload Failed")
        self.setModal(True)
        self.resize(460, 220)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(14)

        title_label = QLabel("Failed to Upload Report", self)
        font = title_label.font()
        font.setBold(True)
        font.setPointSize(font.pointSize() + 1)
        title_label.setFont(font)
        layout.addWidget(title_label)

        msg_label = QLabel(
            f"An error occurred while uploading:\n\n{error_message}\n\n"
            "Would you like to save the report to a file instead?",
            self,
        )
        msg_label.setWordWrap(True)
        layout.addWidget(msg_label)

        layout.addStretch()

        button_row = QHBoxLayout()
        button_row.addStretch()

        close_btn = AppButton("Close", self)
        close_btn.clicked.connect(self.reject)
        button_row.addWidget(close_btn)

        save_btn = AppButton("Save to file…", self)
        save_btn.setObjectName("primaryAction")
        save_btn.clicked.connect(self.accept)
        button_row.addWidget(save_btn)

        layout.addLayout(button_row)


class _SaveReportFileDialog(QFileDialog):
    def __init__(self, parent: QWidget) -> None:
        super().__init__(
            parent,
            "Save Diagnostic Report",
            "pxmodrim-report.txt",
            "Text Files (*.txt);;All Files (*)",
        )
        self.setAcceptMode(QFileDialog.AcceptMode.AcceptSave)


async def _save_report_via_file_dialog(
    parent: QWidget, report: str, toast: ToastManager
) -> None:
    """Prompt user for a destination file and write the report."""
    result, dialog = await await_dialog(_SaveReportFileDialog, parent)
    files = dialog.selectedFiles()
    if result != QDialog.DialogCode.Accepted or not files:
        return

    path = Path(files[0])
    try:
        await asyncio.to_thread(path.write_text, report, "utf-8")
    except OSError as exc:
        toast.error(f"Failed to save file: {exc}", 5000)
        return
    toast.success(f"Report saved to {path.name}", 3000)


async def handle_upload_report(
    parent: QWidget, ctx: CoreContext | Any, toast: ToastManager
) -> str | None:
    """Confirm, then upload or save the report; return the uploaded URL if any."""
    choice, _ = await await_dialog(UploadConfirmDialog, parent)
    if choice not in (QDialog.DialogCode.Accepted, UploadConfirmDialog.SAVE_TO_FILE):
        return None

    report = await asyncio.to_thread(build_report, ctx)

    if choice == UploadConfirmDialog.SAVE_TO_FILE:
        await _save_report_via_file_dialog(parent, report, toast)
        return None

    toast.info("Uploading log & system info…", 2000)
    cfg = getattr(ctx, "config", None)
    endpoint = getattr(cfg, "log_upload_endpoint", "") or _DEFAULT_ENDPOINT
    try:
        url = await PasteUploader(endpoint=endpoint).async_upload(report)
    except (ReportUploadError, OSError, RuntimeError) as exc:
        toast.error(f"Upload failed: {exc}", 5000)
        failed_result, _ = await await_dialog(UploadFailedDialog, str(exc), parent)
        if failed_result == QDialog.DialogCode.Accepted:
            await _save_report_via_file_dialog(parent, report, toast)
        return None

    clipboard = QGuiApplication.clipboard()
    if clipboard is not None:
        clipboard.setText(url)
    toast.success(f"Link copied: {url}", 5000)
    return url
