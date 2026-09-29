from __future__ import annotations

from collections.abc import Callable, Iterable
from dataclasses import dataclass
from typing import TYPE_CHECKING

from loguru import logger
from PySide6.QtGui import QAction
from PySide6.QtWidgets import QDialog, QMenu, QMessageBox, QWidget

from pxmodrim.core.services.workshop_download_service import workshop_service

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext


def updatable_ids(ctx: CoreContext, uuids: Iterable[str]) -> list[str]:
    """Workshop ids among the mods *uuids* that PxModRim can update."""
    svc = workshop_service(ctx)
    if svc is None:
        return []
    all_mods = ctx.all_mods
    return svc.updatable_ids(all_mods[u] for u in uuids if u in all_mods)


CONFIRM_THRESHOLD = 25


@dataclass(frozen=True, slots=True)
class UpdateState:
    label: str
    enabled: bool
    tooltip: str


def update_state(ctx: CoreContext, uuids: Iterable[str]) -> UpdateState:
    selected = list(uuids)
    ids = updatable_ids(ctx, selected)
    label = "Update from Workshop"
    if len(selected) > 1:
        label += f" ({len(ids)} of {len(selected)})"
    svc = workshop_service(ctx)
    if svc is None:
        return UpdateState(label, False, "Workshop downloads are disabled")
    if svc.is_downloading:
        return UpdateState(label, False, "A Workshop download is in progress")
    if not ids:
        return UpdateState(
            label, False, "None of these mods were downloaded by PxModRim"
        )
    return UpdateState(label, True, "")


def add_update_action(
    menu: QMenu, ctx: CoreContext, uuids: Iterable[str], handler: Callable[[], object]
) -> QAction:
    state = update_state(ctx, uuids)
    action = menu.addAction(state.label, handler)
    action.setEnabled(state.enabled)
    action.setToolTip(state.tooltip)
    return action


class ConfirmWorkshopUpdateDialog(QMessageBox):
    def __init__(self, count: int, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setIcon(QMessageBox.Icon.Question)
        QDialog.setWindowTitle(self, "Update Workshop Mods")
        self.setText(f"Re-sync {count} Workshop mods?")
        self.setInformativeText(
            "Every mod downloaded by PxModRim is checked against Steam and "
            "changed files are downloaded. Mods from Steam's own Workshop "
            "folder are not included. This can take a long time."
        )
        self.setStandardButtons(
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.Cancel
        )
        self.setDefaultButton(QMessageBox.StandardButton.Cancel)
        yes = self.button(QMessageBox.StandardButton.Yes)
        if yes:
            yes.setText("Update")
            yes.setObjectName("primaryAction")


async def update_workshop_mods(ctx: CoreContext, uuids: Iterable[str]) -> None:
    """Re-sync the updatable mods among *uuids*; results are toasted by MainWindow."""
    svc = workshop_service(ctx)
    ids = updatable_ids(ctx, uuids)
    if svc is None or not ids:
        logger.debug("[workshop] update requested but nothing is updatable")
        return
    logger.info("[workshop] update requested for {} mods", len(ids))
    try:
        await svc.download_mods(ids)
    except (RuntimeError, ValueError) as exc:
        logger.warning("[workshop] update not started: {}", exc)
