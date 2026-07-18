from __future__ import annotations

from importlib.resources import files as resource_files
from typing import TYPE_CHECKING

from loguru import logger
from PySide6.QtCore import Qt, QUrl
from PySide6.QtWebEngineCore import (
    QWebEnginePage,
    QWebEngineProfile,
    QWebEngineSettings,
)
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtWidgets import QLabel, QWidget

from pxmodrim.core.context import CoreContext
from pxmodrim.ui.views.base import BaseViewPanel

if TYPE_CHECKING:
    from PySide6.QtQml import QQmlEngine

_WORKSHOP_URL = "https://steamcommunity.com/workshop/browse/?appid=294100"


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
        self._initialized = False
        self._first_load = True

        self._placeholder = QLabel("Initializing Steam Workshop browser…")
        self._placeholder.setObjectName("placeholder")
        self._placeholder.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._placeholder.setWordWrap(True)
        self._root.addWidget(self._placeholder, stretch=1)

    def installed_mod_ids(self) -> list[str]:
        return [
            m.published_file_id
            for m in self._ctx.all_mods.values()
            if m.published_file_id
        ]

    def _ensure_initialized(self) -> None:
        if self._initialized:
            return
        self._initialized = True

        # Передаем self в качестве родителя, чтобы Qt сам корректно управлял памятью
        self._profile = QWebEngineProfile("pxmodrim-steam", self)
        settings = self._profile.settings()

        settings.setAttribute(QWebEngineSettings.WebAttribute.DnsPrefetchEnabled, True)
        settings.setAttribute(QWebEngineSettings.WebAttribute.PluginsEnabled, False)
        settings.setAttribute(QWebEngineSettings.WebAttribute.PdfViewerEnabled, False)
        settings.setAttribute(QWebEngineSettings.WebAttribute.FullScreenSupportEnabled, False)
        settings.setAttribute(QWebEngineSettings.WebAttribute.HyperlinkAuditingEnabled, False)
        settings.setAttribute(QWebEngineSettings.WebAttribute.XSSAuditingEnabled, False)
        settings.setAttribute(QWebEngineSettings.WebAttribute.ErrorPageEnabled, False)
        settings.setAttribute(QWebEngineSettings.WebAttribute.AutoLoadIconsForPage, False)

        # Включаем LocalStorage, иначе Steam сломается
        settings.setAttribute(QWebEngineSettings.WebAttribute.LocalStorageEnabled, True)

        self._profile.setHttpCacheType(QWebEngineProfile.HttpCacheType.DiskHttpCache)
        self._profile.setHttpCacheMaximumSize(512 * 1024 * 1024)

        # Создаем WebView и сразу встраиваем в структуру UI, но прячем
        self._web = QWebEngineView(self)
        self._web.setObjectName("workshopWeb")
        self._web.hide()

        page = QWebEnginePage(self._profile, self._web)
        self._web.setPage(page)

        # Добавляем в layout заранее, чтобы избежать прыжков интерфейса
        self._root.addWidget(self._web, stretch=1)

        page.loadProgress.connect(self._on_page_progress)
        self._web.loadStarted.connect(self._on_load_started)
        self._web.loadFinished.connect(self._on_load_finished)
        self._web.loadProgress.connect(self._on_load_progress)

        logger.debug(f"[steam] navigating to {_WORKSHOP_URL}")
        self._web.load(QUrl(_WORKSHOP_URL))

    def _on_page_progress(self, progress: int) -> None:
        if progress < 100:
            if self._first_load:
                self._placeholder.setText(
                    f"Initializing Steam Workshop browser… {progress}%"
                )

    def preload(self) -> None:
        self._ensure_initialized()

    def _run_badge_script(self) -> None:
        if self._web is None:
            return
        inject_js = (
            resource_files("pxmodrim.ui.views.steam_workshop") / "inject.js"
        ).read_text(encoding="utf-8")
        self._web.page().runJavaScript(
            inject_js, 0, lambda r: logger.debug(f"[steam] badge script result: {r!r}")
        )

    def refresh_badges(self) -> None:
        if self._web is None:
            return
        ids = self.installed_mod_ids()
        logger.debug(f"[steam] refresh_badges: {len(ids)} installed")
        self._web.page().runJavaScript(
            f"window.__pxmodrimInstalledMods = {ids!r};"
            "if (window.updateAllModBadges) window.updateAllModBadges();",
            0,
            lambda result: logger.debug(f"[steam] refresh result: {result!r}"),
        )

    def _on_load_started(self) -> None:
        logger.debug("[steam] loadStarted")
        if self._first_load:
            if self._web is not None:
                self._web.hide()
            self._placeholder.setText("Initializing Steam Workshop browser…")
            self._placeholder.show()

    def _on_load_progress(self, progress: int) -> None:
        if progress < 100 and self._first_load:
            self._placeholder.setText(
                f"Initializing Steam Workshop browser… {progress}%"
            )

    def _on_load_finished(self, ok: bool) -> None:
        logger.debug(f"[steam] loadFinished ok={ok}")
        if not ok:
            if self._web is not None:
                self._web.hide()
            self._placeholder.setText(
                "Failed to load Steam Workshop.\n\n"
                "Check your network connection and try again."
            )
            self._placeholder.show()
            return

        self._first_load = False
        self._placeholder.hide()
        if self._web is not None:
            self._web.show()

        self._run_badge_script()
        self.refresh_badges()

    def showEvent(self, event: object) -> None:
        super().showEvent(event)  # type: ignore[attr-defined]
        self._ensure_initialized()

    def teardown(self) -> None:
        """Безопасное уничтожение тяжелых виджетов."""
        if self._web is None:
            return

        self._web.stop()
        self._root.removeWidget(self._web)

        # Вместо ручного удаления профиля через deleteLater,
        # Qt сам очистит его, так как мы указали родителя (self) в `__init__`.
        self._web.setParent(None)
        self._web.deleteLater()
        self._web = None
        self._profile = None
        self._initialized = False
