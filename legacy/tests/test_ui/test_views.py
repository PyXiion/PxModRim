from __future__ import annotations

from collections.abc import Iterator
from typing import TYPE_CHECKING

import pytest
from PySide6.QtCore import QCoreApplication
from PySide6.QtQml import QQmlEngine
from PySide6.QtWidgets import QApplication, QWidget

from pxmodrim.core.config import AppConfig, ConfigService
from pxmodrim.ui.components import create_qml_engine
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
    engine = create_qml_engine()
    theme = Theme(engine)
    engine.rootContext().setContextProperty("Theme", theme)
    yield engine


@pytest.fixture
def parent_widget(qapp: QApplication) -> Iterator[QWidget]:
    """A container the panel is embedded in, mirroring MainWindow's stack.

    Mirrors production wiring (AGENTS.md: never orphan Qt objects) and keeps
    the panel's C++ objects alive for the duration of the test instead of
    being torn down by Python GC while the shared QML engine still holds
    bindings into them.
    """
    widget = QWidget()
    yield widget
    widget.deleteLater()


def _ctx_stub(cfg_svc: ConfigService) -> CoreContext:
    from pxmodrim.core.context import CoreContext

    return CoreContext.create(cfg_svc.load("config.json", AppConfig), cfg_svc)


def _app_ctx_stub(cfg_svc: ConfigService) -> AppContext:
    from pxmodrim.ui.context import AppContext

    return AppContext(_ctx_stub(cfg_svc))


class TestSteamWorkshopView:
    def test_preload_runs_without_error(
        self,
        qml_engine: QQmlEngine,
        config_service: ConfigService,
        parent_widget: QWidget,
    ) -> None:
        view = SteamWorkshopViewPanel(
            ctx=_ctx_stub(config_service),
            qml_engine=qml_engine,
            parent=parent_widget,
            app_ctx=_app_ctx_stub(config_service),
        )

        view.preload()

    def test_web_is_none_before_init(
        self,
        qml_engine: QQmlEngine,
        config_service: ConfigService,
        parent_widget: QWidget,
    ) -> None:
        view = SteamWorkshopViewPanel(
            ctx=_ctx_stub(config_service),
            qml_engine=qml_engine,
            parent=parent_widget,
            app_ctx=_app_ctx_stub(config_service),
        )

        assert view._web() is None
