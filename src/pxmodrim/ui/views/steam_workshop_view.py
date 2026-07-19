from __future__ import annotations

import json
from pathlib import Path
from typing import TYPE_CHECKING

from loguru import logger
from PySide6.QtCore import Qt, QUrl
from PySide6.QtWebChannel import QWebChannel
from PySide6.QtWebEngineCore import (
    QWebEnginePage,
    QWebEngineProfile,
    QWebEngineScript,
    QWebEngineSettings,
)
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QStackedWidget,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.core.context import CoreContext
from pxmodrim.ui.components.icon_button import IconButton
from pxmodrim.ui.views.base import BaseViewPanel
from pxmodrim.ui.views.steam_workshop.bridge import SteamWorkshopBridge
from pxmodrim.ui.views.steam_workshop.download_sidebar import DownloadSidebar

if TYPE_CHECKING:
    from PySide6.QtQml import QQmlEngine

from importlib.resources import files as resource_files

_WORKSHOP_URL = "https://steamcommunity.com/workshop/browse/?appid=294100"

_QWEBCHANNEL_PATH = Path("/usr/share/qt6/webchannel/qwebchannel.js")


def _read_qwebchannel_js() -> str:
    try:
        return _QWEBCHANNEL_PATH.read_text(encoding="utf-8")
    except FileNotFoundError:
        logger.warning("[steam] qwebchannel.js not found at {}", _QWEBCHANNEL_PATH)
        return ""


class SteamWorkshopViewPanel(BaseViewPanel):
    view_id = "steam_workshop"
    icon_name = "steam_workshop_tab"
    label = "Steam Workshop"

    def __init__(
        self,
        ctx: CoreContext,
        qml_engine: QQmlEngine | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(ctx, qml_engine, parent)

        self._web: QWebEngineView | None = None
        self._profile: QWebEngineProfile | None = None
        self._channel: QWebChannel | None = None
        self._bridge: SteamWorkshopBridge | None = None
        self._initialized = False
        self._first_load = True

        self._toolbar: QWidget | None = None
        self._nav_back: IconButton | None = None
        self._nav_forward: IconButton | None = None
        self._nav_reload: IconButton | None = None
        self._url_bar: QLineEdit | None = None

        self._installed_ids: set[str] = set()
        self._checked_ids: dict[str, str] = {}

        content = QWidget()
        h_layout = QHBoxLayout(content)
        h_layout.setContentsMargins(0, 0, 0, 0)
        h_layout.setSpacing(0)

        self._download_sidebar = DownloadSidebar(self._qml_engine, content)
        self._download_sidebar.download_requested.connect(self._on_download_requested)
        self._download_sidebar.item_removed.connect(self._on_download_item_removed)
        h_layout.addWidget(self._download_sidebar)

        right_side = QWidget()
        right_layout = QVBoxLayout(right_side)
        right_layout.setContentsMargins(0, 0, 0, 0)
        right_layout.setSpacing(0)

        self._toolbar = self._create_toolbar(right_side)
        right_layout.addWidget(self._toolbar)

        self._stack = QStackedWidget()
        self._placeholder = QLabel("Initializing Steam Workshop browser\u2026")
        self._placeholder.setObjectName("placeholder")
        self._placeholder.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._placeholder.setWordWrap(True)
        self._stack.addWidget(self._placeholder)
        right_layout.addWidget(self._stack, stretch=1)

        h_layout.addWidget(right_side, stretch=1)

        self._root.addWidget(content, stretch=1)

        self._ctx.mod_service.mods_changed.connect(self._on_mods_changed)

    def installed_mod_ids(self) -> list[str]:
        return [
            m.published_file_id
            for m in self._ctx.all_mods.values()
            if m.published_file_id
        ]

    def _refresh_cached_ids(self) -> None:
        self._installed_ids = set(self.installed_mod_ids())

    def _ensure_initialized(self) -> None:
        if self._initialized:
            return
        self._initialized = True

        self._profile = QWebEngineProfile("pxmodrim-steam", self)
        settings = self._profile.settings()

        settings.setAttribute(QWebEngineSettings.WebAttribute.DnsPrefetchEnabled, True)
        settings.setAttribute(QWebEngineSettings.WebAttribute.PluginsEnabled, False)
        settings.setAttribute(QWebEngineSettings.WebAttribute.PdfViewerEnabled, False)
        settings.setAttribute(
            QWebEngineSettings.WebAttribute.FullScreenSupportEnabled, False
        )
        settings.setAttribute(
            QWebEngineSettings.WebAttribute.HyperlinkAuditingEnabled, False
        )
        settings.setAttribute(
            QWebEngineSettings.WebAttribute.XSSAuditingEnabled, False
        )
        settings.setAttribute(
            QWebEngineSettings.WebAttribute.ErrorPageEnabled, False
        )
        settings.setAttribute(
            QWebEngineSettings.WebAttribute.AutoLoadIconsForPage, False
        )
        settings.setAttribute(QWebEngineSettings.WebAttribute.LocalStorageEnabled, True)

        self._profile.setHttpCacheType(QWebEngineProfile.HttpCacheType.DiskHttpCache)
        self._profile.setHttpCacheMaximumSize(512 * 1024 * 1024)

        qwebchannel_src = _read_qwebchannel_js()
        if qwebchannel_src:
            script = QWebEngineScript()
            script.setSourceCode(qwebchannel_src)
            script.setInjectionPoint(QWebEngineScript.InjectionPoint.DocumentCreation)
            script.setWorldId(QWebEngineScript.ScriptWorldId.MainWorld)
            script.setRunsOnSubFrames(True)
            self._profile.scripts().insert(script)

        inject_js = (
            resource_files("pxmodrim.ui.views.steam_workshop") / "inject.js"
        ).read_text(encoding="utf-8")
        script2 = QWebEngineScript()
        script2.setSourceCode(inject_js)
        script2.setInjectionPoint(QWebEngineScript.InjectionPoint.DocumentCreation)
        script2.setWorldId(QWebEngineScript.ScriptWorldId.MainWorld)
        script2.setRunsOnSubFrames(False)
        self._profile.scripts().insert(script2)

        self._web = QWebEngineView(self)
        self._web.setObjectName("workshopWeb")
        self._stack.addWidget(self._web)

        page = QWebEnginePage(self._profile, self._web)
        self._web.setPage(page)

        self._bridge = SteamWorkshopBridge(self, self._web)
        self._channel = QWebChannel(self._web)
        self._channel.registerObject("bridge", self._bridge)
        page.setWebChannel(self._channel)

        self._bridge.download_state_changed.connect(self._on_download_state_changed)

        page.loadProgress.connect(self._on_page_progress)
        self._web.loadStarted.connect(self._on_load_started)
        self._web.loadFinished.connect(self._on_load_finished)
        page.urlChanged.connect(self._on_url_changed)

        assert self._nav_back is not None
        assert self._nav_forward is not None
        assert self._nav_reload is not None
        self._nav_back.clicked.connect(self._web.back)
        self._nav_forward.clicked.connect(self._web.forward)
        self._nav_reload.clicked.connect(self._web.reload)

        self._refresh_cached_ids()

        logger.debug(f"[steam] navigating to {_WORKSHOP_URL}")
        self._web.load(QUrl(_WORKSHOP_URL))

    def _on_page_progress(self, progress: int) -> None:
        if progress < 100 and self._first_load:
            self._placeholder.setText(
                f"Initializing Steam Workshop browser\u2026 {progress}%"
            )

    def _create_toolbar(self, parent: QWidget) -> QWidget:
        toolbar = QWidget(parent)
        toolbar.setObjectName("workshopToolbar")
        layout = QHBoxLayout(toolbar)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(4)

        self._nav_back = IconButton("chevron-left", "Back", size=28, parent=toolbar)
        self._nav_back.setEnabled(False)
        layout.addWidget(self._nav_back)

        self._nav_forward = IconButton("chevron", "Forward", size=28, parent=toolbar)
        self._nav_forward.setEnabled(False)
        layout.addWidget(self._nav_forward)

        self._nav_reload = IconButton("refresh", "Reload", size=28, parent=toolbar)
        layout.addWidget(self._nav_reload)

        self._url_bar = QLineEdit(toolbar)
        self._url_bar.setPlaceholderText("Enter a URL\u2026")
        self._url_bar.returnPressed.connect(self._navigate_to_url)
        layout.addWidget(self._url_bar, stretch=1)

        return toolbar

    def _navigate_to_url(self) -> None:
        if self._web is None or self._url_bar is None:
            return
        text = self._url_bar.text().strip()
        if not text:
            return
        url = QUrl.fromUserInput(text)
        if url.scheme() == "http":
            url.setScheme("https")
        if not url.isValid():
            logger.warning(f"[steam] invalid URL: {text}")
            return
        self._web.load(url)

    def _on_url_changed(self, url: QUrl) -> None:
        if self._url_bar is None:
            return
        self._url_bar.setText(url.toString())
        self._update_nav_buttons()

    def _update_nav_buttons(self) -> None:
        if self._web is None or self._nav_back is None or self._nav_forward is None:
            return
        history = self._web.page().history()
        self._nav_back.setEnabled(history.canGoBack())
        self._nav_forward.setEnabled(history.canGoForward())

    def preload(self) -> None:
        self._ensure_initialized()

    def refresh_badges(self) -> None:
        if self._web is None:
            return
        self._refresh_cached_ids()
        self._web.page().runJavaScript(
            f"window.__pxmSetInstalled({json.dumps(list(self._installed_ids))});",
            0,
            lambda result: logger.debug(f"[steam] refresh result: {result!r}"),
        )

    def _on_load_started(self) -> None:
        logger.debug("[steam] loadStarted")
        if self._first_load:
            self._placeholder.setText("Initializing Steam Workshop browser\u2026")
            self._stack.setCurrentWidget(self._placeholder)

    def _on_load_finished(self, ok: bool) -> None:
        logger.debug(f"[steam] loadFinished ok={ok}")
        if not ok:
            self._placeholder.setText(
                "Failed to load Steam Workshop.\n\n"
                "Check your network connection and try again."
            )
            self._stack.setCurrentWidget(self._placeholder)
            return

        self._first_load = False
        if self._web is not None:
            self._stack.setCurrentWidget(self._web)
        self._update_nav_buttons()

    def _on_mods_changed(self, _: None) -> None:
        if self._web is None:
            return
        self.refresh_badges()

    def _on_download_requested(self) -> None:
        logger.debug(f"[steam] download requested: {list(self._checked_ids.keys())}")

    def _on_download_item_removed(self, mod_id: str) -> None:
        self._checked_ids.pop(mod_id, None)
        self._download_sidebar.sync_from(self._checked_ids)
        if self._web:
            self._web.page().runJavaScript(
                f"window.__pxmUncheckMod({json.dumps(mod_id)});",
                0,
            )

    def _on_download_state_changed(
        self, mod_id: str, title: str, checked: bool
    ) -> None:
        self._download_sidebar.sync_from(self._checked_ids)

    def showEvent(self, event: object) -> None:
        super().showEvent(event)  # type: ignore[attr-defined]
        self._ensure_initialized()

    def teardown(self) -> None:
        self._ctx.mod_service.mods_changed.disconnect(self._on_mods_changed)

        if self._web is None:
            return

        self._web.stop()
        self._stack.removeWidget(self._web)
        self._web.setParent(None)
        self._web.deleteLater()
        self._web = None
        self._profile = None
        self._channel = None
        self._bridge = None
        self._initialized = False
