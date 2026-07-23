from __future__ import annotations

from collections.abc import Iterator
from typing import TYPE_CHECKING

import pytest
from PySide6.QtCore import QCoreApplication
from PySide6.QtWidgets import QApplication

from pxmodrim.core.config import AppConfig
from pxmodrim.ui.views.steam_workshop_view import SteamWorkshopViewPanel

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext

pytestmark = pytest.mark.usefixtures("qapp")


@pytest.fixture(scope="module")
def qapp() -> Iterator[QApplication]:
    app = QCoreApplication.instance()
    if app is None:
        app = QApplication([])
    assert isinstance(app, QApplication)
    yield app


def _ctx_stub() -> CoreContext:
    from pxmodrim.core.context import CoreContext

    return CoreContext.create(_cfg())


def _cfg() -> AppConfig:
    from pxmodrim.core.config import config_file_path, load_config

    return load_config(config_file_path())


class TestSteamWorkshopView:
    def test_refresh_badges_runs_without_error(self, qapp: QApplication) -> None:
        view = SteamWorkshopViewPanel(ctx=_ctx_stub())
        qapp.processEvents()

        view.preload()
        qapp.processEvents()

        view.refresh_badges()
        qapp.processEvents()

    def test_refresh_badges_safe_before_init(self, qapp: QApplication) -> None:
        view = SteamWorkshopViewPanel(ctx=_ctx_stub())
        qapp.processEvents()

        view.refresh_badges()
        assert view._web() is None
