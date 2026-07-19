from __future__ import annotations

from collections.abc import Iterator
from importlib import resources as importlib_resources
from typing import TYPE_CHECKING

import pytest
from PySide6.QtCore import QCoreApplication
from PySide6.QtWidgets import QApplication, QLabel, QVBoxLayout, QWidget

from pxmodrim.core.config import AppConfig
from pxmodrim.ui.views import SteamWorkshopViewPanel, builtin_views

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


@pytest.fixture
def host(qapp: QApplication) -> Iterator[tuple[QWidget, QVBoxLayout]]:
    w = QWidget()
    layout = QVBoxLayout(w)
    w.show()
    qapp.processEvents()
    yield (w, layout)
    w.hide()
    qapp.processEvents()


def _ctx_stub() -> CoreContext:
    from pxmodrim.core.context import CoreContext

    return CoreContext.create(_cfg())


def _cfg() -> AppConfig:
    from pxmodrim.core.config import config_file_path, load_config

    return load_config(config_file_path())


class TestBuiltinViews:
    def test_registers_mods_and_steam(self) -> None:
        ids = [v.view_id for v in builtin_views()]
        assert "mods" in ids
        assert "steam_workshop" in ids
        assert ids.index("mods") < ids.index("steam_workshop")

    def test_steam_view_metadata(self) -> None:
        assert SteamWorkshopViewPanel.view_id == "steam_workshop"
        assert SteamWorkshopViewPanel.icon_name == "steam_workshop_tab"
        assert SteamWorkshopViewPanel.label == "Steam Workshop"

    def test_constructs_with_download_sidebar(self, qapp: QApplication) -> None:
        view = SteamWorkshopViewPanel(ctx=_ctx_stub())
        qapp.processEvents()
        assert view._download_sidebar is not None


class TestSteamWorkshopView:
    def test_constructs_with_placeholder(
        self, qapp: QApplication
    ) -> None:
        view = SteamWorkshopViewPanel(ctx=_ctx_stub())
        qapp.processEvents()

        assert isinstance(view, QWidget)
        placeholder = view.findChild(QLabel, "placeholder")
        assert placeholder is not None
        assert "Initializing" in placeholder.text()
        # WebEngine must NOT be initialized until the tab is shown
        assert view._web is None
        assert view._initialized is False

    def test_webengine_init_on_show(
        self, qapp: QApplication
    ) -> None:
        view = SteamWorkshopViewPanel(ctx=_ctx_stub())
        qapp.processEvents()

        view.show()
        qapp.processEvents()

        assert view._initialized is True
        assert view._web is not None

    def test_preload_starts_webengine(
        self, qapp: QApplication
    ) -> None:
        view = SteamWorkshopViewPanel(ctx=_ctx_stub())
        qapp.processEvents()

        # preload() must initialize WebEngine without a show event
        view.preload()
        qapp.processEvents()

        assert view._initialized is True
        assert view._web is not None

        # preload() is idempotent: a second call must not recreate the view
        first_web = view._web
        view.preload()
        qapp.processEvents()
        assert view._web is first_web

    def test_profile_is_named_persistent(
        self, qapp: QApplication
    ) -> None:
        view = SteamWorkshopViewPanel(ctx=_ctx_stub())
        qapp.processEvents()

        view.preload()
        qapp.processEvents()

        assert view._profile is not None
        assert view._profile.storageName() == "pxmodrim-steam"
        assert view._profile.persistentStoragePath() != ""

    def test_refresh_badges_runs_without_error(
        self, qapp: QApplication
    ) -> None:
        view = SteamWorkshopViewPanel(ctx=_ctx_stub())
        qapp.processEvents()

        view.preload()
        qapp.processEvents()

        # refresh_badges must run without raising once initialized.
        view.refresh_badges()
        qapp.processEvents()

        inject_js = (
            importlib_resources.files("pxmodrim.ui.views.steam_workshop")
            / "inject.js"
        ).read_text(encoding="utf-8")
        assert "updateAllModBadges" in inject_js
        assert "__pxmSetInstalled" in inject_js
        assert "__pxmUncheckMod" in inject_js
        assert "MutationObserver" in inject_js
        assert "__onInstalledChanged" not in inject_js
        assert "__onCheckedChanged" not in inject_js
        assert "__onSingleModChecked" not in inject_js

    def test_refresh_badges_safe_before_init(
        self, qapp: QApplication
    ) -> None:
        view = SteamWorkshopViewPanel(ctx=_ctx_stub())
        qapp.processEvents()

        # No WebEngine yet: must no-op without raising so the app-state
        # watcher can fire before the Steam tab is ever opened.
        view.refresh_badges()
        assert view._web is None

    def test_inject_js_targets_react_dom(self) -> None:
        inject_js = (
            importlib_resources.files("pxmodrim.ui.views.steam_workshop")
            / "inject.js"
        ).read_text(encoding="utf-8")

        # Steam Workshop is a React SPA with hashed class names; badges must
        # target the stable anchors (item links + thumbnail <img>), not the
        # removed .workshopItem markup.
        assert 'sharedfiles/filedetails/?id=' in inject_js
        assert "querySelector(\"img\")" in inject_js
        assert ".workshopItem" not in inject_js
        assert ".workshopItemTitle" not in inject_js
        assert "rimsort-badge-hovered" not in inject_js
