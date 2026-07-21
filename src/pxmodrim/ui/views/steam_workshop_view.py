from __future__ import annotations

import json
from importlib.resources import files as resource_files
from pathlib import Path
from typing import TYPE_CHECKING, cast

from loguru import logger
from PySide6.QtCore import QObject, QUrl, Signal, Slot
from PySide6.QtGui import QColor
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import QHBoxLayout, QWidget
from qasync import asyncSlot

from pxmodrim.core.context import CoreContext
from pxmodrim.core.services.steam_cmd_service import (
    SteamCmdItemStatus,
    SteamCmdProgress,
    SteamCmdResult,
)
from pxmodrim.ui.theme.palette import PALETTE
from pxmodrim.ui.ui_prefs import UIPrefs
from pxmodrim.ui.views.base import BaseViewPanel
from pxmodrim.ui.views.steam_workshop.bridge import SteamWorkshopBridge
from pxmodrim.ui.views.steam_workshop.download_sidebar import DownloadSidebar

if TYPE_CHECKING:
    from PySide6.QtQml import QQmlEngine

    from pxmodrim.ui.plugins.steam_cmd_plugin import SteamCmdUiPlugin

_WORKSHOP_URL = "https://steamcommunity.com/workshop/browse/?appid=294100"

_QML_DIR = Path(__file__).parent / "steam_workshop"
_STEAM_WORKSHOP_QML = str(_QML_DIR / "SteamWorkshop.qml")

_QWEBCHANNEL_JS = (
    resource_files("pxmodrim.ui.views.steam_workshop") / "qwebchannel.js"
).read_text(encoding="utf-8")

_INJECT_JS = (
    resource_files("pxmodrim.ui.views.steam_workshop") / "inject.js"
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

        self._bridge: SteamWorkshopBridge | None = None
        self._initialized = False

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

        self._qml = QQuickWidget(self._qml_engine, content)  # pyright: ignore[reportCallIssue, reportArgumentType]
        self._qml.setObjectName("workshopView")
        self._qml.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        self._qml.setClearColor(QColor(PALETTE["ELEVATE_0"]))
        h_layout.addWidget(self._qml, stretch=1)

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

    # ── Installed mod helpers ────────────────────────────

    def installed_mod_ids(self) -> list[str]:
        return [
            m.published_file_id
            for m in self._ctx.all_mods.values()
            if m.published_file_id
        ]

    def _refresh_cached_ids(self) -> None:
        self._installed_ids = set(self.installed_mod_ids())

    # ── QML object access ────────────────────────────────

    def _web(self) -> QObject | None:
        if not self._initialized:
            return None
        root = self._qml.rootObject()
        if root is None:
            return None
        return root.findChild(QObject, "workshopWeb")

    # ── Initialization ───────────────────────────────────

    def _ensure_initialized(self) -> None:
        if self._initialized:
            return
        self._initialized = True

        self._bridge = SteamWorkshopBridge(self, self)

        qml_ctx = self._qml.rootContext()
        qml_ctx.setContextProperty("steamWorkshopPanel", self)
        qml_ctx.setContextProperty("_qwebchannelCode", _QWEBCHANNEL_JS)
        qml_ctx.setContextProperty("_injectCode", _INJECT_JS)

        self._qml.setSource(_STEAM_WORKSHOP_QML)

        root = self._qml.rootObject()
        if root is not None:
            channel = root.findChild(QObject, "workshopWebChannel")
            if channel is not None:
                channel.registerObject("bridge", self._bridge)  # pyright: ignore[reportAttributeAccessIssue]

        self._bridge.download_state_changed.connect(self._on_download_state_changed)
        self._refresh_cached_ids()

    # ── QML-invokable slots ──────────────────────────────

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

    def refresh_badges(self) -> None:
        web = self._web()
        if web is None:
            return
        self._refresh_cached_ids()
        web.runJavaScript(  # pyright: ignore[reportAttributeAccessIssue]
            f"window.__pxmSetInstalled({json.dumps(list(self._installed_ids))});",
            lambda _result: None,
        )

    # ── Event handlers ───────────────────────────────────

    def _on_mods_changed(self, _: None) -> None:
        self.refresh_badges()

    # ── Download handlers ────────────────────────────────

    @asyncSlot()
    async def _on_download_requested(self) -> None:
        ids = list(self._checked_ids.keys())
        if not ids:
            logger.debug("[steam] download requested with empty queue")
            return
        logger.debug("[steam] download requested: %s", ids)

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
        logger.debug("[steam] download item removed: %s", mod_id)
        self._checked_ids.pop(mod_id, None)
        self._download_statuses.pop(mod_id, None)
        self._download_sidebar.sync_from(self._checked_ids, self._download_statuses)
        web = self._web()
        if web is not None:
            web.runJavaScript(  # pyright: ignore[reportAttributeAccessIssue]
                f"window.__pxmUncheckMod({json.dumps(mod_id)});",
                lambda _result: None,
            )

    def _on_clear_requested(self) -> None:
        logger.info("[steam] download queue cleared (%d items)", len(self._checked_ids))
        self._checked_ids.clear()
        self._download_statuses.clear()
        self._download_sidebar.sync_from(self._checked_ids, self._download_statuses)
        web = self._web()
        if web is not None:
            web.runJavaScript(  # pyright: ignore[reportAttributeAccessIssue]
                "window.__pxmClearChecked();",
                lambda _result: None,
            )

    def _on_download_state_changed(
        self, mod_id: str, title: str, checked: bool
    ) -> None:
        logger.debug(
            "[steam] download_state_changed: %s checked=%s queue=%d",
            mod_id,
            checked,
            len(self._checked_ids),
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
            "[steam] download finished: %d ok, %d failed",
            len(result.succeeded),
            len(result.failed),
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

    # ── Lifecycle ────────────────────────────────────────

    def showEvent(self, event: object) -> None:
        logger.debug("[steam] view shown")
        super().showEvent(event)  # type: ignore[attr-defined]
        self._ensure_initialized()

    def teardown(self) -> None:
        logger.debug("[steam] view teardown")
        svc = self._ctx.steam_cmd_service

        try:
            svc.download_progress.disconnect(self._on_steam_progress)
            svc.download_item_status_changed.disconnect(self._on_steam_item_status)
            svc.download_finished.disconnect(self._on_steam_finished)
            self._ctx.mod_service.mods_changed.disconnect(self._on_mods_changed)
        except (TypeError, RuntimeError):
            pass

        if not self._initialized:
            return

        web = self._web()
        if web is not None:
            web.stop()  # pyright: ignore[reportAttributeAccessIssue]
            web.setProperty("url", QUrl("about:blank"))

        ctx = self._qml.rootContext()
        ctx.setContextProperty("webChannel", None)

        self._qml.setSource(QUrl())
        self._qml.setParent(None)
        self._qml.deleteLater()

        self._bridge = None
        self._channel = None
        self._initialized = False
