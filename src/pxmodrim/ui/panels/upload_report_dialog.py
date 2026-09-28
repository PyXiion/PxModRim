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


class UploadConfirmDialog(QDialog):
    """Confirmation dialog explaining what will be uploaded and anonymized."""

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("uploadConfirmDialog")
        self.setWindowTitle("Upload Log & System Info")
        self.setModal(True)
        self.resize(520, 320)

        self.choice: str = "cancel"

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
            "  • Home directory paths are replaced with '~'\n"
            "  • Operating system username is replaced with '<user>'\n\n"
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
        self._save_file_btn.clicked.connect(self._on_save_file)
        button_row.addWidget(self._save_file_btn)

        button_row.addStretch()

        cancel_btn = AppButton("Cancel", self)
        cancel_btn.clicked.connect(self.reject)
        button_row.addWidget(cancel_btn)

        self._upload_btn = AppButton("Upload", self)
        self._upload_btn.setObjectName("primaryAction")
        self._upload_btn.clicked.connect(self._on_upload)
        button_row.addWidget(self._upload_btn)

        layout.addLayout(button_row)

    def _on_upload(self) -> None:
        self.choice = "upload"
        self.accept()

    def _on_save_file(self) -> None:
        self.choice = "save"
        self.accept()


class UploadFailedDialog(QDialog):
    """Error dialog shown on upload failure offering saving to a file instead."""

    def __init__(self, error_message: str, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("uploadFailedDialog")
        self.setWindowTitle("Upload Failed")
        self.setModal(True)
        self.resize(460, 220)

        self.choice: str = "close"

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
        save_btn.clicked.connect(self._on_save)
        button_row.addWidget(save_btn)

        layout.addLayout(button_row)

    def _on_save(self) -> None:
        self.choice = "save"
        self.accept()


async def _save_report_via_file_dialog(
    parent: QWidget, report: str, toast: ToastManager | None = None
) -> bool:
    """Prompt user for a destination file and write the report."""
    file_path, _ = QFileDialog.getSaveFileName(
        parent,
        "Save Diagnostic Report",
        "pxmodrim-report.txt",
        "Text Files (*.txt);;All Files (*)",
    )
    if not file_path:
        return False

    try:
        await asyncio.to_thread(Path(file_path).write_text, report, "utf-8")
        if toast is not None:
            toast.success(f"Report saved to {Path(file_path).name}", 3000)
        return True
    except OSError as exc:
        if toast is not None:
            toast.error(f"Failed to save file: {exc}", 5000)
        return False


async def handle_upload_report(
    parent: QWidget,
    ctx: CoreContext | Any,
    toast_manager: ToastManager | None = None,
) -> str | None:
    """Execute the upload confirmation, upload / save-to-file, and clipboard flow."""
    # Find toast manager if not provided
    toast = toast_manager
    if toast is None and hasattr(parent, "window"):
        win = parent.window()
        toast = getattr(win, "_toast_manager", None)

    # 1. Confirmation dialog
    result, dialog = await await_dialog(UploadConfirmDialog, parent)
    if result != QDialog.DialogCode.Accepted:
        return None

    # 2. Build report in background thread
    report = await asyncio.to_thread(build_report, ctx)

    # 3. User selected "Save to file instead"
    if dialog.choice == "save":
        await _save_report_via_file_dialog(parent, report, toast)
        return None

    # 4. User confirmed upload
    if dialog.choice == "upload":
        if toast is not None:
            toast.info("Uploading log & system info…", 2000)

        cfg = getattr(ctx, "config", None)
        endpoint = (
            getattr(cfg, "log_upload_endpoint", "")
            if cfg is not None
            else "https://paste.rs/"
        )
        uploader = PasteUploader(endpoint=endpoint or "https://paste.rs/")

        try:
            url = await uploader.async_upload(report)
        except (ReportUploadError, OSError, RuntimeError) as exc:
            if toast is not None:
                toast.error(f"Upload failed: {exc}", 5000)

            # Offer saving to file on failure
            failed_res, failed_dlg = await await_dialog(
                UploadFailedDialog, str(exc), parent
            )
            if (
                failed_res == QDialog.DialogCode.Accepted
                and failed_dlg.choice == "save"
            ):
                await _save_report_via_file_dialog(parent, report, toast)
            return None

        # 5. Success: copy to clipboard and notify
        clipboard = QGuiApplication.clipboard()
        if clipboard is not None:
            clipboard.setText(url)
        if toast is not None:
            toast.success(f"Link copied: {url}", 5000)
        return url

    return None
