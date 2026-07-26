from __future__ import annotations

import json
from pathlib import Path
from typing import TYPE_CHECKING, cast

from loguru import logger
from PySide6.QtCore import QObject, QUrl, Slot
from PySide6.QtGui import QColor
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import QHBoxLayout, QWidget
from qasync import asyncSlot

from pxmodrim.core.context import CoreContext
from pxmodrim.ui.plugins.steam_workshop.download_sidebar import DownloadSidebar
from pxmodrim.ui.theme.palette import PALETTE
from pxmodrim.ui.views.base import BaseViewPanel

if TYPE_CHECKING:
    from PySide6.QtQml import QQmlEngine

    from pxmodrim.ui.context import AppContext
    from pxmodrim.ui.plugins.steam_workshop.plugin import (
        DepsResult,
        ItemStatus,
        ProgressInfo,
        SidebarSync,
        SteamCmdUiPlugin,
    )

_WORKSHOP_URL = "https://steamcommunity.com/workshop/browse/?appid=294100"

_QML_DIR = Path(__file__).parent
_STEAM_WORKSHOP_QML = str(_QML_DIR / "SteamWorkshop.qml")


class SteamWorkshopViewPanel(BaseViewPanel):
    view_id = "steam_workshop"
    icon_name = "steam_workshop_tab"
    label = "Steam Workshop"

    def __init__(
        self,
        ctx: CoreContext,
        qml_engine: QQmlEngine | None = None,
        parent: QWidget | None = None,
        app_ctx: AppContext | None = None,
    ) -> None:
        super().__init__(ctx, qml_engine, parent, app_ctx=app_ctx)

        self._plugin: SteamCmdUiPlugin | None = None
        self._initialized = False

        content = QWidget()
        h_layout = QHBoxLayout(content)
        h_layout.setContentsMargins(0, 0, 0, 0)
        h_layout.setSpacing(0)

        self._download_sidebar = DownloadSidebar(self._qml_engine, content)
        h_layout.addWidget(self._download_sidebar)

        self._qml = QQuickWidget(self._qml_engine, content)  # pyright: ignore[reportCallIssue, reportArgumentType]
        self._qml.setObjectName("workshopView")
        self._qml.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        self._qml.setClearColor(QColor(PALETTE["ELEVATE_0"]))
        h_layout.addWidget(self._qml, stretch=1)

        self._root.addWidget(content, stretch=1)

    # ── QML object access ────────────────────────────────

    def _web(self) -> QObject | None:
        if not self._initialized:
            return None
        root = self._qml.rootObject()
        if root is None:
            return None
        return root.findChild(QObject, "workshopWeb")

    def _run_js(self, code: str) -> None:
        web = self._web()
        if web is not None:
            web.runJavaScript(code, 0)  # pyright: ignore[reportAttributeAccessIssue]

    # ── Initialization ───────────────────────────────────

    def _ensure_initialized(self) -> None:
        if self._initialized:
            return
        self._initialized = True

        self._plugin = cast(
            "SteamCmdUiPlugin | None",
            self._app_ctx.plugins.get("steamworkshop") if self._app_ctx else None,
        )
        if self._plugin is not None:
            self._download_sidebar.download_requested.connect(
                self._on_download_requested
            )
            self._download_sidebar.stop_requested.connect(
                self._plugin.stop_download
            )
            self._download_sidebar.item_removed.connect(
                self._plugin.remove_item
            )
            self._download_sidebar.clear_requested.connect(
                self._plugin.clear_queue
            )

            self._plugin.badges_refresh_requested.connect(
                self._push_badges_to_js
            )
            self._plugin.deps_result_ready.connect(self._push_deps_to_js)
            self._plugin.sidebar_sync_requested.connect(self._on_sidebar_sync)
            self._plugin.progress_updated.connect(self._on_progress_updated)
            self._plugin.item_status_changed.connect(
                self._on_item_status_changed
            )
            self._plugin.download_busy_changed.connect(
                lambda busy: self._download_sidebar.set_download_enabled(not busy)
            )
            self._plugin.uncheck_mod_requested.connect(
                self._push_uncheck_to_js
            )
            self._plugin.clear_checked_requested.connect(
                self._push_clear_checked_to_js
            )

        qml_ctx = self._qml.rootContext()
        qml_ctx.setContextProperty("steamWorkshopPanel", self)

        self._qml.setSource(_STEAM_WORKSHOP_QML)

    # ── QML-invokable slots ──────────────────────────────

    @Slot()
    def onPageLoaded(self) -> None:
        logger.debug("[steam] page loaded")
        if self._plugin is not None:
            self._plugin.sync_all()

    @Slot()
    def navigateHome(self) -> None:
        web = self._web()
        if web is not None:
            web.setProperty("url", QUrl(_WORKSHOP_URL))

    @Slot(str)
    def navigateToUrl(self, text: str) -> None:
        if not text:
            return
        url = QUrl.fromUserInput(text)
        if url.scheme() == "http":
            url.setScheme("https")
        if not url.isValid():
            logger.warning("[steam] invalid URL: %s", text)
            return
        logger.debug("[steam] navigating to URL: %s", url.toString())
        web = self._web()
        if web is not None:
            web.setProperty("url", url)

    # ── Public API ───────────────────────────────────────

    def preload(self) -> None:
        self._ensure_initialized()

    # ── Plugin event handlers ────────────────────────────

    def _push_badges_to_js(self, ids: list[str]) -> None:
        self._run_js(f"window.__pxmSetInstalled({json.dumps(ids)});")

    def _push_deps_to_js(self, result: DepsResult) -> None:
        mod_id_json = json.dumps(result.mod_id)
        js = f"window.__pxmDepsFetched({mod_id_json}, {result.json_result});"
        self._run_js(js)

    def _push_uncheck_to_js(self, mod_id: str) -> None:
        self._run_js(f"window.__pxmUncheckMod({json.dumps(mod_id)});")

    def _push_clear_checked_to_js(self, _: None) -> None:
        self._run_js("window.__pxmClearChecked();")

    def _on_sidebar_sync(self, sync: SidebarSync) -> None:
        self._download_sidebar.sync_from(sync.checked_ids, sync.statuses)

    def _on_progress_updated(self, info: ProgressInfo) -> None:
        self._download_sidebar.set_progress(
            info.total, info.completed, info.downloading_id
        )

    def _on_item_status_changed(self, status: ItemStatus) -> None:
        self._download_sidebar.update_status(status.mod_id, status.status)

    # ── Sidebar signal handler ───────────────────────────

    @asyncSlot()
    async def _on_download_requested(self) -> None:
        if self._plugin is not None:
            await self._plugin.request_download(self, self._ui_prefs.validate_downloads)

    # ── Lifecycle ────────────────────────────────────────

    def showEvent(self, event: object) -> None:
        logger.debug("[steam] view shown")
        super().showEvent(event)  # type: ignore[attr-defined]
        self._ensure_initialized()

    def teardown(self) -> None:
        logger.debug("[steam] view teardown")

        if not self._initialized:
            return

        web = self._web()
        if web is not None:
            web.stop()  # pyright: ignore[reportAttributeAccessIssue]
            web.setProperty("url", QUrl("about:blank"))

        self._qml.setSource(QUrl())
        self._qml.setParent(None)
        self._qml.deleteLater()

        self._plugin = None
        self._initialized = False
