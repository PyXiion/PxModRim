from __future__ import annotations

from collections.abc import Iterable
from typing import TYPE_CHECKING

from PySide6.QtWidgets import QMessageBox, QWidget

from pxmodrim.core.models.metadata.structures import AboutXmlMod, ListedMod
from pxmodrim.ui.components.dialogs import await_dialog

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext


def _dependent_detail(mod: ListedMod) -> str:
    if isinstance(mod, AboutXmlMod):
        return f"{mod.name} ({mod.package_id})"
    return mod.name


class DependentModsDialog(QMessageBox):
    def __init__(self, dependents: list[str], parent: QWidget) -> None:
        super().__init__(parent)
        self.setIcon(QMessageBox.Icon.Warning)
        self.setWindowTitle("Disable Dependent Mods")
        self.setText(
            "These active mods depend on the mod you are disabling:\n\n"
            + "\n".join(f"• {dependent}" for dependent in dependents)
        )
        self.setInformativeText(
            "Choose whether to disable only the selected mod or these dependents too."
        )
        self.setStandardButtons(
            QMessageBox.StandardButton.Yes
            | QMessageBox.StandardButton.No
            | QMessageBox.StandardButton.Cancel
        )
        self.setDefaultButton(QMessageBox.StandardButton.No)
        self.setButtonText(
            QMessageBox.StandardButton.Yes, "Disable selected + dependents"
        )
        self.setButtonText(QMessageBox.StandardButton.No, "Disable selected only")
        yes_button = self.button(QMessageBox.StandardButton.Yes)
        if yes_button is not None:
            yes_button.setObjectName("primaryAction")


async def apply_activation(
    ctx: CoreContext,
    parent: QWidget,
    enable: Iterable[str] = (),
    disable: Iterable[str] = (),
) -> bool:
    """Enable/disable mods in one core call, confirming active dependents first.

    Every UI surface that toggles mods goes through here so the dependents
    prompt cannot diverge between views. Returns whether active state changed.
    """
    enabling = list(enable)
    disabling = list(disable)
    activation = ctx.activation
    dependents = activation.dependents_of(disabling, enable=enabling)
    if dependents:
        all_mods = ctx.all_mods
        details = [_dependent_detail(all_mods[uuid]) for uuid in dependents]
        result, _ = await await_dialog(DependentModsDialog, details, parent)
        if result == QMessageBox.StandardButton.Cancel:
            return False
        if result == QMessageBox.StandardButton.Yes:
            disabling.extend(dependents)
    return activation.apply(enable=enabling, disable=disabling)


async def toggle_mods(ctx: CoreContext, parent: QWidget, uuids: Iterable[str]) -> bool:
    """Flip each mod: active ones are disabled, inactive ones enabled."""
    active = set(ctx.active_uuids)
    selected = list(uuids)
    return await apply_activation(
        ctx,
        parent,
        enable=[uuid for uuid in selected if uuid not in active],
        disable=[uuid for uuid in selected if uuid in active],
    )
