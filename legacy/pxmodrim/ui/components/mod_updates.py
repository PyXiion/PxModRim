from __future__ import annotations

from collections.abc import Callable, Iterable
from dataclasses import dataclass
from typing import TYPE_CHECKING

from loguru import logger
from PySide6.QtGui import QAction
from PySide6.QtWidgets import QDialog, QMenu, QMessageBox, QWidget

from pxmodrim.core.downloads import download_manager
from pxmodrim.ui.components.dialogs import await_dialog

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext


def updatable_ids(ctx: CoreContext, uuids: Iterable[str]) -> list[str]:
    """Source ids among the mods *uuids* that PxModRim can update."""
    all_mods = ctx.all_mods
    return download_manager(ctx).updatable_ids(
        all_mods[u] for u in uuids if u in all_mods
    )


CONFIRM_THRESHOLD = 25


@dataclass(frozen=True, slots=True)
class UpdateState:
    label: str
    enabled: bool
    tooltip: str


def update_state(ctx: CoreContext, uuids: Iterable[str]) -> UpdateState:
    selected = list(uuids)
    ids = updatable_ids(ctx, selected)
    label = "Update"
    if len(selected) > 1:
        label += f" ({len(ids)} of {len(selected)})"
    downloads = download_manager(ctx)
    if not downloads.available:
        return UpdateState(label, False, "No download sources are enabled")
    if downloads.is_downloading:
        return UpdateState(label, False, "A download is in progress")
    if not ids:
        return UpdateState(
            label, False, "None of these mods were downloaded by PxModRim"
        )
    return UpdateState(label, True, "")


def add_update_action(
    menu: QMenu, ctx: CoreContext, uuids: Iterable[str], handler: Callable[[], object]
) -> QAction | None:
    """Add an Update entry to *menu*; skipped when no download source exists."""
    if not download_manager(ctx).available:
        return None
    state = update_state(ctx, uuids)
    action = menu.addAction(state.label, handler)
    action.setEnabled(state.enabled)
    action.setToolTip(state.tooltip)
    return action


class ConfirmUpdateDialog(QMessageBox):
    def __init__(self, count: int, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setIcon(QMessageBox.Icon.Question)
        QDialog.setWindowTitle(self, "Update Mods")
        self.setText(f"Update {count} mods?")
        self.setInformativeText(
            "Every mod downloaded by PxModRim is checked against its source and "
            "changed files are downloaded. Mods managed by other tools, such as "
            "Steam's own Workshop folder, are not included. This can take a "
            "long time."
        )
        self.setStandardButtons(
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.Cancel
        )
        self.setDefaultButton(QMessageBox.StandardButton.Cancel)
        yes = self.button(QMessageBox.StandardButton.Yes)
        if yes:
            yes.setText("Update")
            yes.setObjectName("primaryAction")


async def confirm_update(count: int, parent: QWidget) -> bool:
    """Ask before a large update; small ones go through unasked."""
    if count < CONFIRM_THRESHOLD:
        return True
    confirmed, _ = await await_dialog(ConfirmUpdateDialog, count, parent)
    return confirmed == QMessageBox.StandardButton.Yes


async def update_mods(ctx: CoreContext, uuids: Iterable[str], parent: QWidget) -> None:
    """Update the updatable mods among *uuids*; results are toasted by MainWindow."""
    downloads = download_manager(ctx)
    ids = updatable_ids(ctx, uuids)
    if not ids:
        logger.debug("[downloads] update requested but nothing is updatable")
        return
    if not await confirm_update(len(ids), parent):
        return
    logger.info("[downloads] update requested for {} mods", len(ids))
    try:
        await downloads.download_mods(ids)
    except (RuntimeError, ValueError) as exc:
        logger.warning("[downloads] update not started: {}", exc)
