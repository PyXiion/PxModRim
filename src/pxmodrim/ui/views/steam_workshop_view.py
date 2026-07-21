from __future__ import annotations

import json
from typing import TYPE_CHECKING, cast

from loguru import logger
from PySide6.QtCore import Qt, QUrl, Signal
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
from qasync import asyncSlot

from pxmodrim.core.context import CoreContext
from pxmodrim.core.services.steam_cmd_service import (
    SteamCmdItemStatus,
    SteamCmdProgress,
    SteamCmdResult,
)
from pxmodrim.ui.components.icon_button import IconButton
from pxmodrim.ui.theme.palette import PALETTE
from pxmodrim.ui.ui_prefs import UIPrefs
from pxmodrim.ui.views.base import BaseViewPanel
from pxmodrim.ui.views.steam_workshop.bridge import SteamWorkshopBridge
from pxmodrim.ui.views.steam_workshop.download_sidebar import DownloadSidebar

if TYPE_CHECKING:
    from PySide6.QtQml import QQmlEngine

    from pxmodrim.ui.plugins.steam_cmd_plugin import SteamCmdUiPlugin

from importlib.resources import files as resource_files

_WORKSHOP_URL = "https://steamcommunity.com/workshop/browse/?appid=294100"

_QWEBCHANNEL_JS = (
    resource_files("pxmodrim.ui.views.steam_workshop") / "qwebchannel.js"
).read_text(encoding="utf-8")


class SteamWorkshopViewPanel(BaseViewPanel):
    view_id = "steam_workshop"
    icon_name = "steam_workshop_tab"
    label = "Steam Workshop"

    _refresh_requested = Signal()

    def __init__(
        self,
        ctx: CoreContext,
        qml_engine: QQmlEngine | None = None,
        parent: QWidget | None = None,
        ui_prefs: UIPrefs | None = None,
    ) -> None:
        super().__init__(ctx, qml_engine, parent, ui_prefs=ui_prefs)

        self._web: QWebEngineView | None = None
        self._profile: QWebEngineProfile | None = None
        self._channel: QWebChannel | None = None
        self._bridge: SteamWorkshopBridge | None = None
        self._initialized = False
        self._first_load = True
        self._page_loading = False

        self._toolbar: QWidget | None = None
        self._nav_home: IconButton | None = None
        self._nav_back: IconButton | None = None
        self._nav_forward: IconButton | None = None
        self._nav_reload: IconButton | None = None
        self._url_bar: QLineEdit | None = None
        self._loading_indicator: QLabel | None = None

        self._installed_ids: set[str] = set()
        self._checked_ids: dict[str, str] = {}
        self._download_statuses: dict[str, str] = {}
        self._current_downloading_id: str = ""

        content = QWidget()
        h_layout = QHBoxLayout(content)
        h_layout.setContentsMargins(0, 0, 0, 0)
        h_layout.setSpacing(0)

        self._download_sidebar = DownloadSidebar(self._qml_engine, content)
        self._download_sidebar.download_requested.connect(self._on_download_requested)
        self._download_sidebar.stop_requested.connect(self._on_stop_requested)
        self._download_sidebar.item_removed.connect(self._on_download_item_removed)
        self._download_sidebar.clear_requested.connect(self._on_clear_requested)
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
        self._ctx.steam_cmd_service.download_progress.connect(
            self._on_steam_progress
        )
        self._ctx.steam_cmd_service.download_item_status_changed.connect(
            self._on_steam_item_status
        )
        self._ctx.steam_cmd_service.download_finished.connect(
            self._on_steam_finished
        )
        self._refresh_requested.connect(self._on_refresh_requested)

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

        script = QWebEngineScript()
        script.setSourceCode(_QWEBCHANNEL_JS)
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

        assert self._nav_home is not None
        assert self._nav_back is not None
        assert self._nav_forward is not None
        assert self._nav_reload is not None
        web = self._web
        assert web is not None
        self._nav_home.clicked.connect(lambda: web.load(QUrl(_WORKSHOP_URL)))
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

        self._nav_home = IconButton("home", "Home", size=28, parent=toolbar)
        layout.addWidget(self._nav_home)

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

        self._loading_indicator = QLabel("\u27F3", toolbar)
        self._loading_indicator.setVisible(False)
        self._loading_indicator.setStyleSheet("color: " + PALETTE["TEXT_DIM"])
        layout.addWidget(self._loading_indicator)

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
        logger.debug(f"[steam] navigating to URL: {url.toString()}")
        self._web.load(url)

    def _on_url_changed(self, url: QUrl) -> None:
        if self._url_bar is None:
            return
        logger.debug(f"[steam] urlChanged: {url.toString()}")
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
        self._page_loading = True
        if self._loading_indicator is not None:
            self._loading_indicator.setVisible(True)
        if self._first_load:
            self._placeholder.setText("Initializing Steam Workshop browser\u2026")
            self._stack.setCurrentWidget(self._placeholder)

    def _on_load_finished(self, ok: bool) -> None:
        logger.debug(f"[steam] loadFinished ok={ok}")
        self._page_loading = False
        if self._loading_indicator is not None:
            self._loading_indicator.setVisible(False)
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

    @asyncSlot()
    async def _on_download_requested(self) -> None:
        ids = list(self._checked_ids.keys())
        if not ids:
            logger.debug("[steam] download requested with empty queue")
            return
        logger.debug(f"[steam] download requested: {ids}")

        plugin = cast(
            "SteamCmdUiPlugin | None", self._ctx.plugins.get("steamcmd")
        )
        if plugin is None:
            logger.debug("[steam] steamcmd plugin not available")
            return

        self._download_sidebar.set_download_enabled(False)
        try:
            if not await plugin.ensure_steamcmd(self):
                return
            if not await plugin.ensure_symlink(self):
                return

            await self._ctx.steam_cmd_service.download_mods(
                ids,
                validate=self._ui_prefs.validate_downloads,
                titles=dict(self._checked_ids),
            )
        finally:
            self._download_sidebar.set_download_enabled(True)

    def _on_stop_requested(self) -> None:
        logger.info("[steam] download stop requested")
        self._ctx.steam_cmd_service.cancel()
        self._current_downloading_id = ""
        self._download_sidebar.set_progress(0, 0, "")

    def _on_download_item_removed(self, mod_id: str) -> None:
        logger.debug(f"[steam] download item removed: {mod_id}")
        self._checked_ids.pop(mod_id, None)
        self._download_statuses.pop(mod_id, None)
        self._download_sidebar.sync_from(self._checked_ids, self._download_statuses)
        if self._web:
            self._web.page().runJavaScript(
                f"window.__pxmUncheckMod({json.dumps(mod_id)});",
                0,
            )

    def _on_clear_requested(self) -> None:
        logger.info(f"[steam] download queue cleared ({len(self._checked_ids)} items)")
        self._checked_ids.clear()
        self._download_statuses.clear()
        self._download_sidebar.sync_from(self._checked_ids, self._download_statuses)
        if self._web:
            self._web.page().runJavaScript(
                "window.__pxmClearChecked();",
                0,
            )

    def _on_download_state_changed(
        self, mod_id: str, title: str, checked: bool
    ) -> None:
        logger.debug(
            f"[steam] download_state_changed: {mod_id} checked={checked}"
            f" queue={len(self._checked_ids)}"
        )
        self._download_sidebar.sync_from(self._checked_ids, self._download_statuses)

    def _on_steam_progress(self, progress: SteamCmdProgress) -> None:
        self._download_sidebar.set_progress(
            progress.total, progress.completed, self._current_downloading_id
        )

    def _on_steam_item_status(self, item: SteamCmdItemStatus) -> None:
        if item.status == "downloading":
            self._current_downloading_id = item.mod_id
        self._download_statuses[item.mod_id] = item.status
        self._download_sidebar.update_status(item.mod_id, item.status)

    def _on_steam_finished(self, result: SteamCmdResult) -> None:
        logger.info(
            f"[steam] download finished: {len(result.succeeded)} ok, "
            f"{len(result.failed)} failed"
        )
        for mid in (*result.succeeded, *result.failed):
            self._checked_ids.pop(mid, None)
            self._download_statuses.pop(mid, None)
        self._current_downloading_id = ""
        self._download_sidebar.set_progress(0, 0, "")
        self._download_sidebar.sync_from(self._checked_ids, self._download_statuses)
        self._refresh_requested.emit()

    @asyncSlot()
    async def _on_refresh_requested(self) -> None:
        await self._ctx.mod_service.discover()


    def showEvent(self, event: object) -> None:
        logger.debug("[steam] view shown")
        super().showEvent(event)  # type: ignore[attr-defined]
        self._ensure_initialized()

    def teardown(self) -> None:
        logger.debug("[steam] view teardown")
        svc = self._ctx.steam_cmd_service
        svc.download_progress.disconnect(self._on_steam_progress)
        svc.download_item_status_changed.disconnect(self._on_steam_item_status)
        svc.download_finished.disconnect(self._on_steam_finished)
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
