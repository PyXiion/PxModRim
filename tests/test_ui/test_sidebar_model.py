from __future__ import annotations

from PySide6.QtCore import QCoreApplication
from PySide6.QtWidgets import QApplication

from pxmodrim.ui.models.sidebar_model import SidebarModel
from pxmodrim.ui.theme.palette import PALETTE


class ProviderEntry:
    def __init__(self, label: str) -> None:
        self.label = label
        self.count = 2


def test_update_entries_refreshes_provider_icon_and_color() -> None:
    app = QCoreApplication.instance()
    if app is None:
        app = QApplication([])
    assert isinstance(app, QApplication)
    model = SidebarModel()
    model.set_entries([ProviderEntry("Steam")])
    changed_roles: list[list[int]] = []
    model.dataChanged.connect(lambda _, __, roles: changed_roles.append(roles))

    model.update_entries([ProviderEntry("Local")])

    assert model.data(model.index(0, 0), SidebarModel.IconRole) == "folder"
    assert (
        model.data(model.index(0, 0), SidebarModel.IconColorRole) == PALETTE["WARNING"]
    )
    assert SidebarModel.IconRole in changed_roles[0]
    assert SidebarModel.IconColorRole in changed_roles[0]
