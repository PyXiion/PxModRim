from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt, QUrl
from PySide6.QtGui import QColor
from PySide6.QtQml import QQmlEngine
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import QDialog, QVBoxLayout, QWidget

from pxmodrim.core.services.startup_impact_service import StartupImpactService
from pxmodrim.core.services.startup_impact_service.labels import metric_label
from pxmodrim.core.services.startup_impact_service.models import (
    StartupImpactMod,
    StartupImpactReport,
)
from pxmodrim.ui.components.dialog_chrome import install_dialog_chrome
from pxmodrim.ui.models.impact import format_duration, impact_color
from pxmodrim.ui.theme.palette import PALETTE

_QML_DIR = Path(__file__).parent
_TIMING_QML = _QML_DIR / "TimeAnalytics.qml"

_COLOR_BASE = PALETTE["TEXT_DIM"]
_COLOR_BASE_GAME = PALETTE["PRIMARY"]
_COLOR_ACCENT = PALETTE["PRIMARY"]


_COLOR_PRESETS = [
    "#e6194b",
    "#3cb44b",
    "#ffe119",
    "#4363d8",
    "#f58231",
    "#911eb4",
    "#42d4f4",
    "#f032e6",
    "#bfef45",
    "#fabed4",
    "#469990",
    "#dcbeff",
    "#9a6324",
    "#800000",
    "#aaffc3",
    "#808000",
    "#ffd8b1",
    "#000075",
    "#a9a9a9",
    "#e6beff",
]


def _metric_color(index: int) -> str:
    return _COLOR_PRESETS[index % len(_COLOR_PRESETS)]


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
        self._qml.setClearColor(QColor(PALETTE["ELEVATE_2"]))
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

        report, base, totals, own = await sis.snapshot(active_pids, pid)
        if request_token != self._request_token:
            return
        if not report:
            root_obj.setProperty("sourceData", None)
            return

        mod = report.find(pid, None) if pid else None
        total = base + sum(on + off for on, off in totals.values())
        is_active = pid in active_pids if pid else False

        if is_active:
            other = max(0.0, total - own - base)
        else:
            other = max(0.0, total - base)
            total = total + own
        mod_count = len(report.mods)

        segments = self._build_segments(mod, own)
        top5_data = self._build_top5(report, mod, own)
        donut_legend = self._build_donut_legend(mod, own, other, base, mod_count)

        data = {
            "segments": segments,
            "estimated_total": format_duration(total - own if not is_active else total),
            "own_impact": format_duration(own),
            "other_time": format_duration(other),
            "donut_bg": base / total if total > 0 else 0,
            "donut_own": own / total if total > 0 else 0,
            "donut_other": other / total if total > 0 else 0,
            "own_color": impact_color(own),
            "other_color": _COLOR_BASE,
            "bg_color": _COLOR_BASE_GAME,
            "donut_legend": donut_legend,
            "top5": top5_data,
            "top5_label": "Top 5 slowest mods",
            "timestamp": report.timestamp or "",
        }
        root_obj.setProperty("sourceData", data)

    def _build_segments(self, mod: StartupImpactMod | None, own: float) -> list[dict]:
        if not mod:
            return []
        denom = max(own, 0.001)
        sorted_m = sorted(mod.metrics.items(), key=lambda x: x[1], reverse=True)
        return [
            {
                "label": metric_label(name),
                "value": format_duration(val_s),
                "fraction": val_s / denom,
                "color": _metric_color(i),
            }
            for i, (name, val_s) in enumerate(sorted_m)
        ]

    def _build_top5(
        self, report: StartupImpactReport, mod: StartupImpactMod | None, own: float
    ) -> list[dict]:
        sorted_mods = sorted(report.mods, key=lambda m: m.total_impact_s, reverse=True)
        top5: list[tuple[StartupImpactMod, bool]] = []
        current_pid = mod.package_id if mod else None
        for m in sorted_mods:
            is_current = m.package_id is not None and m.package_id == current_pid
            if len(top5) < 5 and not is_current:
                top5.append((m, False))
            elif is_current and len(top5) < 5:
                top5.append((m, True))
        if mod and not any(is_cur for _, is_cur in top5):
            if len(top5) >= 5:
                top5[-1] = (mod, True)
            else:
                top5.append((mod, True))
        entries = [
            (entry, is_cur, own if is_cur else entry.total_impact_s)
            for entry, is_cur in top5
        ]
        max_impact = max((val for _, _, val in entries), default=1)
        result = []
        for entry, is_cur, val in entries:
            bar_color = _COLOR_ACCENT if is_cur else impact_color(entry.total_impact_s)
            result.append(
                {
                    "label": entry.mod_name,
                    "value": format_duration(val),
                    "fraction": val / max_impact if max_impact > 0 else 0,
                    "is_current": is_cur,
                    "color": bar_color,
                }
            )
        return result

    def _build_donut_legend(
        self,
        mod: StartupImpactMod | None,
        own: float,
        other: float,
        base: float,
        mod_count: int,
    ) -> list[dict]:
        legend = []
        if own > 0.001:
            legend.append(
                {
                    "label": mod.mod_name if mod else "This mod",
                    "value": format_duration(own),
                    "color": impact_color(own),
                }
            )
        if other > 0.001:
            legend.append(
                {
                    "label": f"Other mods ({mod_count})",
                    "value": format_duration(other),
                    "color": _COLOR_BASE,
                }
            )
        if base > 0.001:
            legend.append(
                {
                    "label": "Base game",
                    "value": format_duration(base),
                    "color": _COLOR_BASE_GAME,
                }
            )
        return legend

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
        self.resize(460, 640)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self.panel = TimeAnalyticsPanel(sis, qml_engine, self)
        layout.addWidget(self.panel)
        install_dialog_chrome(self)
