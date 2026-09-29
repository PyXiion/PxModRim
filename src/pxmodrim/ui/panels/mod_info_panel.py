from __future__ import annotations

import asyncio
from pathlib import Path
from typing import TYPE_CHECKING, Any

from PySide6.QtCore import QMetaObject, Qt, QUrl
from PySide6.QtGui import QColor, QDesktopServices, QGuiApplication, QImage, QPixmap
from PySide6.QtQml import QQmlEngine
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import QLabel, QVBoxLayout, QWidget
from qasync import asyncSlot

from pxmodrim.core.context import CoreContext
from pxmodrim.core.models.metadata.structures import AboutXmlMod, ListedMod
from pxmodrim.core.services.startup_impact_service.labels import metric_label
from pxmodrim.ui.components import AspectRatioBanner, generate_preview
from pxmodrim.ui.models.impact import format_duration, impact_color
from pxmodrim.ui.panels.mod_info_data import build_mod_info
from pxmodrim.ui.panels.time_analytics_panel import StartupImpactDialog
from pxmodrim.ui.theme.constants import BANNER_MAX_HEIGHT
from pxmodrim.ui.theme.palette import PALETTE
from pxmodrim.ui.ui_prefs import UIPrefs

if TYPE_CHECKING:
    from pxmodrim.core.models.view.diagnostics import ModIssueView

_QML = Path(__file__).parent / "ModInfo.qml"
_TOP_METRICS = 3
_NO_STARTUP = {"available": False, "message": "Startup impact is unavailable."}


def _first_sentence(text: str, max_len: int = 80) -> str:
    if not text:
        return ""
    stripped = text.strip()
    end = stripped.find(".")
    result = stripped[: end + 1] if end != -1 else stripped
    if len(result) > max_len:
        result = result[:max_len].rsplit(" ", 1)[0] + "\u2026"
    return result


def _find_preview(mod_path: Path | None) -> Path | None:
    if mod_path is None:
        return None
    candidate = mod_path / "About" / "Preview.png"
    return candidate if candidate.exists() else None


class ModInfoPanel(QWidget):
    def __init__(
        self,
        ctx: CoreContext,
        qml_engine: QQmlEngine | None = None,
        ui_prefs: UIPrefs | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._ctx = ctx
        self._qml_engine = qml_engine
        self._ui_prefs = ui_prefs or UIPrefs()
        self._mod: ListedMod | None = None
        self._current_mod_id: str | None = None
        self._preview_task: asyncio.Task[None] | None = None
        self._startup_token = 0
        self._startup_args: tuple[str | None, list[str]] = (None, [])

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        self._banner = AspectRatioBanner(self, max_height=BANNER_MAX_HEIGHT)
        self._banner.hide()
        layout.addWidget(self._banner, 0, Qt.AlignmentFlag.AlignTop)

        self._placeholder = QLabel("Select a mod to view details", self)
        self._placeholder.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._placeholder.setObjectName("placeholder")
        layout.addWidget(self._placeholder, 0, Qt.AlignmentFlag.AlignTop)

        self._qml = QQuickWidget(qml_engine, self)  # type: ignore[arg-type]
        self._qml.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        self._qml.setClearColor(QColor(PALETTE["ELEVATE_2"]))
        self._qml.setSource(QUrl.fromLocalFile(str(_QML)))
        self._qml.hide()
        layout.addWidget(self._qml, 1)

        root: Any = self._qml.rootObject()
        if root is not None:
            root.setProperty("descExpanded", self._ui_prefs.desc_expanded)
            root.openFolder.connect(self._on_open_folder)
            root.openUrl.connect(self._on_open_url)
            root.copyText.connect(self._on_copy_text)
            root.openStartupDetails.connect(self._on_open_startup_details)
            root.descToggled.connect(self._on_desc_toggled)

    def _set_qml(self, name: str, value: object) -> None:
        root = self._qml.rootObject()
        if root is not None:
            root.setProperty(name, value)

    def show_mod(self, mod: ListedMod, issues: list[ModIssueView]) -> None:
        if self._preview_task and not self._preview_task.done():
            self._preview_task.cancel()

        mod_id = getattr(mod, "package_id", None)
        if mod_id is None:
            mod_id = mod.name

        self._mod = mod
        self._current_mod_id = mod_id

        self._placeholder.hide()
        self._banner.show()
        self._qml.show()

        self._banner.setTitle(mod.name)
        self._banner.setSubtitle(
            _first_sentence(mod.description)
            if mod.description
            else (str(mod.package_id) if isinstance(mod, AboutXmlMod) else "")
        )

        self._set_qml("startup", None)
        self.set_issues(issues)
        root: Any = self._qml.rootObject()
        if root is not None:
            QMetaObject.invokeMethod(root, "resetScroll")

        self._preview_task = asyncio.ensure_future(
            self._load_preview(mod.mod_path, mod_id, mod.name)
        )

    def set_issues(self, issues: list[ModIssueView]) -> None:
        if self._mod is None:
            return
        self._set_qml("info", build_mod_info(self._ctx, self._mod, issues))

    def _on_open_folder(self) -> None:
        if self._mod is not None and self._mod.mod_path is not None:
            QDesktopServices.openUrl(QUrl.fromLocalFile(str(self._mod.mod_path)))

    def _on_open_url(self) -> None:
        if isinstance(self._mod, AboutXmlMod) and self._mod.url:
            QDesktopServices.openUrl(QUrl(self._mod.url))

    def _on_copy_text(self, text: str) -> None:
        QGuiApplication.clipboard().setText(text)

    @asyncSlot()
    async def _on_open_startup_details(self) -> None:
        pid, active_pids = self._startup_args
        dialog = StartupImpactDialog(
            self._ctx.mod_service.startup_impact, self._qml_engine, self
        )
        dialog.show()
        await dialog.panel.set_data(pid, active_pids)

    async def _load_preview(
        self, mod_path: Path | None, mod_id: str, mod_name: str
    ) -> None:
        """Load preview image in background thread. Only updates UI if current mod."""
        try:
            preview_path = await asyncio.to_thread(_find_preview, mod_path)

            if preview_path is not None:
                image = await asyncio.to_thread(QImage, str(preview_path))
                if self._current_mod_id == mod_id and not image.isNull():
                    self._banner.setPixmap(QPixmap.fromImage(image))
                    self._banner.setShowOverlay(False)
                    return

            # No usable preview — show fallback with overlay
            if self._current_mod_id == mod_id:
                w = self._banner.width() or 300
                h = self._banner.sizeHint().height()
                fallback = await asyncio.to_thread(generate_preview, mod_name, w, h)
                if self._current_mod_id == mod_id:
                    self._banner.setShowOverlay(True)
                    self._banner.setPixmap(fallback)
        except asyncio.CancelledError:
            pass

    async def set_time_analytics(
        self,
        pid: str | None,
        active_pids: list[str],
    ) -> None:
        self._startup_token += 1
        token = self._startup_token
        self._startup_args = (pid, active_pids)

        sis = self._ctx.mod_service.startup_impact
        if not sis or not pid:
            self._set_qml("startup", _NO_STARTUP)
            return

        report, *_, own = await sis.snapshot(active_pids, pid)
        if token != self._startup_token:
            return

        if report is None:
            self._set_qml(
                "startup",
                {
                    "available": False,
                    "message": "No startup data recorded yet. "
                    "Launch the game to generate it.",
                },
            )
            return

        mod = report.find(pid, None)
        if mod is None or own < 0.001:
            self._set_qml(
                "startup",
                {
                    "available": False,
                    "message": "This mod wasn't measured in the last recorded launch.",
                },
            )
            return

        rank = 1 + sum(1 for m in report.mods if m.total_impact_s > own)
        top = sorted(mod.metrics.items(), key=lambda kv: kv[1], reverse=True)
        self._set_qml(
            "startup",
            {
                "available": True,
                "own": format_duration(own),
                "rank": f"#{rank} slowest of {len(report.mods)} mods",
                "estimated": pid.lower() not in {a.lower() for a in active_pids},
                "color": impact_color(own),
                "rows": [
                    {
                        "label": metric_label(name),
                        "value": format_duration(value),
                        "fraction": min(1.0, value / own),
                    }
                    for name, value in top[:_TOP_METRICS]
                    if value >= 0.001
                ],
            },
        )

    def clear(self) -> None:
        self._mod = None
        self._current_mod_id = None
        self._startup_token += 1
        self._placeholder.show()
        self._banner.hide()
        self._qml.hide()
        self._set_qml("info", None)
        self._set_qml("startup", None)

    @asyncSlot(bool)
    async def _on_desc_toggled(self, expanded: bool) -> None:
        self._ui_prefs.desc_expanded = expanded
        from pxmodrim.ui.config import save_ui_prefs

        await asyncio.to_thread(save_ui_prefs, self._ui_prefs, self._ctx.config_service)
