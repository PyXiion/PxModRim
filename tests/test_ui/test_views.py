from __future__ import annotations

from collections.abc import Iterator
from typing import TYPE_CHECKING

import pytest
from PySide6.QtCore import QCoreApplication
from PySide6.QtQml import QQmlEngine
from PySide6.QtWidgets import QApplication

from pxmodrim.core.config import AppConfig
from pxmodrim.ui.plugins.steam_workshop import SteamWorkshopViewPanel
from pxmodrim.ui.theme.qml_theme import Theme

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext
    from pxmodrim.ui.context import AppContext

pytestmark = pytest.mark.usefixtures("qapp")


@pytest.fixture(scope="module")
def qapp() -> Iterator[QApplication]:
    app = QCoreApplication.instance()
    if app is None:
        app = QApplication([])
    assert isinstance(app, QApplication)
    yield app


@pytest.fixture(scope="module")
def qml_engine(qapp: QApplication) -> Iterator[QQmlEngine]:
    engine = QQmlEngine()
    theme = Theme(engine)
    engine.rootContext().setContextProperty("Theme", theme)
    yield engine


def _ctx_stub() -> CoreContext:
    from pxmodrim.core.context import CoreContext

    return CoreContext.create(_cfg())


def _app_ctx_stub() -> AppContext:
    from pxmodrim.ui.context import AppContext

    return AppContext(_ctx_stub())


def _cfg() -> AppConfig:
    from pxmodrim.core.config import config_file_path, load_config

    return load_config(config_file_path())


class TestSteamWorkshopView:
    def test_preload_runs_without_error(
        self, qml_engine: QQmlEngine
    ) -> None:
        view = SteamWorkshopViewPanel(
            ctx=_ctx_stub(), qml_engine=qml_engine, app_ctx=_app_ctx_stub(),
        )

        view.preload()

    def test_web_is_none_before_init(self, qml_engine: QQmlEngine) -> None:
        view = SteamWorkshopViewPanel(
            ctx=_ctx_stub(), qml_engine=qml_engine, app_ctx=_app_ctx_stub(),
        )

        assert view._web() is None
