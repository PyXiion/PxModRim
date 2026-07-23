from __future__ import annotations

from typing import TYPE_CHECKING

from PySide6.QtWidgets import QFileDialog, QMessageBox, QWidget
from qasync import asyncSlot

from pxmodrim.core.loading import LoadingState
from pxmodrim.core.plugin import Plugin
from pxmodrim.core.services.steam_cmd_service import SymlinkConflictError
from pxmodrim.ui.components.dialogs import await_dialog
from pxmodrim.ui.components.progress_dialog import ProgressDialog
from pxmodrim.ui.views.steam_workshop_view import SteamWorkshopViewPanel

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext


class SteamCmdUiPlugin(Plugin):
    name = "steamcmd"

    def setup(self, ctx: CoreContext) -> None:
        self._ctx = ctx
        ctx.add_rail_view(SteamWorkshopViewPanel)

    async def init(self, ctx: CoreContext) -> None:
        ...

    async def shutdown(self) -> None:
        pass

    @asyncSlot()
    async def ensure_steamcmd(
        self, parent: QWidget, prefix: str | None = None
    ) -> bool:
        """Prompt user to install SteamCMD if missing."""
        svc = self._ctx.steam_cmd_service
        if svc.is_installed():
            return True

        if prefix is None:
            prefix = self._ctx.config.paths.steamcmd_prefix
        if not prefix:
            folder = QFileDialog.getExistingDirectory(
                parent, "Select SteamCMD install folder"
            )
            if not folder:
                return False
            prefix = folder

        async with ProgressDialog(LoadingState(), parent) as dialog:
            return await svc.ensure_installed(
                prefix, loading_state=dialog.loading
            )

    @asyncSlot()
    async def ensure_symlink(
        self, parent: QWidget, target: str | None = None, forced: bool = False
    ) -> bool:
        svc = self._ctx.steam_cmd_service
        local = target or self._ctx.config.paths.local
        if not local:
            svc.status_message_changed.emit(
                "Local mods path is not configured."
            )
            return False

        try:
            await svc.ensure_symlink(local, forced=forced)
            return True
        except OSError:
            await await_dialog(
                QMessageBox,
                QMessageBox.Icon.Critical,
                "Symlink Error",
                "Failed to create SteamCMD symlink. Check permissions or disk space.",
                QMessageBox.StandardButton.Ok,
                parent,
            )
            return False
        except SymlinkConflictError as exc:
            result, _ = await await_dialog(
                QMessageBox,
                QMessageBox.Icon.Warning,
                "Overwrite existing folder?",
                str(exc),
                QMessageBox.StandardButton.Yes
                | QMessageBox.StandardButton.No,
                parent,
            )
            if result != QMessageBox.StandardButton.Yes:
                svc.status_message_changed.emit(
                    "Download cancelled by user."
                )
                return False

            await svc.ensure_symlink(local, forced=True)
            return True
