from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt, QUrl
from PySide6.QtGui import QColor
from PySide6.QtQml import QQmlEngine
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import QDialog, QVBoxLayout, QWidget
from shiboken6 import isValid

from pxmodrim.core.services.startup_impact_service import StartupImpactService
from pxmodrim.ui.components.dialog_chrome import install_dialog_chrome
from pxmodrim.ui.panels.startup_impact_data import build_startup_impact
from pxmodrim.ui.theme.palette import PALETTE

_TIMING_QML = Path(__file__).parent / "TimeAnalytics.qml"


class TimeAnalyticsPanel(QWidget):
    def __init__(
        self,
        sis: StartupImpactService | None = None,
        qml_engine: QQmlEngine | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._sis = sis
        self._request_token = 0
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        self._qml = QQuickWidget(qml_engine, self)  # type: ignore[arg-type]
        self._qml.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        self._qml.setAttribute(Qt.WidgetAttribute.WA_AlwaysStackOnTop, False)
        self._qml.setClearColor(QColor(PALETTE["ELEVATE_1"]))
        self._qml.setSource(QUrl.fromLocalFile(str(_TIMING_QML)))
        layout.addWidget(self._qml)

    async def set_data(self, pid: str | None, active_pids: list[str]) -> None:
        self._request_token += 1
        request_token = self._request_token
        root_obj = self._qml.rootObject()
        if not root_obj:
            return

        sis = self._sis
        if not sis:
            root_obj.setProperty("sourceData", None)
            return

        report, base, totals, _ = await sis.snapshot(active_pids)
        if request_token != self._request_token or not isValid(root_obj):
            return
        if not report:
            root_obj.setProperty("sourceData", None)
            return

        estimated = base + sum(on + off for on, off in totals.values())
        root_obj.setProperty(
            "sourceData",
            build_startup_impact(
                report,
                active_pids=active_pids,
                selected_pid=pid,
                estimated_s=estimated,
            ),
        )

    def clear(self) -> None:
        self._request_token += 1
        root_obj = self._qml.rootObject()
        if root_obj:
            root_obj.setProperty("sourceData", None)


class StartupImpactDialog(QDialog):
    """Non-modal window showing the full startup impact breakdown."""

    def __init__(
        self,
        sis: StartupImpactService | None,
        qml_engine: QQmlEngine | None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.setWindowTitle("Startup impact")
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.resize(900, 720)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self.panel = TimeAnalyticsPanel(sis, qml_engine, self)
        layout.addWidget(self.panel)
        install_dialog_chrome(self)
