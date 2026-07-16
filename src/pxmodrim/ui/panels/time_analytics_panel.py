from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor
from PySide6.QtQml import QQmlEngine
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import QVBoxLayout, QWidget

from pxmodrim.core.services.startup_impact_service import StartupImpactService
from pxmodrim.core.services.startup_impact_service.labels import metric_label
from pxmodrim.core.services.startup_impact_service.models import (
    StartupImpactMod,
    StartupImpactReport,
)
from pxmodrim.ui.theme.palette import PALETTE

_QML_DIR = Path(__file__).parent
_TIMING_QML = _QML_DIR / "TimeAnalytics.qml"

_COLOR_BASE = "#6b7280"
_COLOR_BASE_GAME = PALETTE["PRIMARY"]
_COLOR_LOW = PALETTE["SUCCESS"]
_COLOR_MED = PALETTE["WARNING"]
_COLOR_HIGH = PALETTE["DANGER"]
_COLOR_ACCENT = PALETTE["PRIMARY"]


def _impact_color(seconds: float) -> str:
    return _COLOR_LOW if seconds < 1.0 else _COLOR_MED if seconds < 5.0 else _COLOR_HIGH


_COLOR_PRESETS = [
    "#e6194b", "#3cb44b", "#ffe119", "#4363d8", "#f58231",
    "#911eb4", "#42d4f4", "#f032e6", "#bfef45", "#fabed4",
    "#469990", "#dcbeff", "#9a6324", "#800000", "#aaffc3",
    "#808000", "#ffd8b1", "#000075", "#a9a9a9", "#e6beff",
]

def _metric_color(index: int) -> str:
    return _COLOR_PRESETS[index % len(_COLOR_PRESETS)]


def _fmt(seconds: float) -> str:
    if seconds < 0.001:
        return "0ms"
    if seconds < 1:
        return f"{round(seconds * 1000)}ms"
    return f"{seconds:.2f}s"


class TimeAnalyticsPanel(QWidget):
    def __init__(
        self,
        sis: StartupImpactService | None = None,
        qml_engine: QQmlEngine | None = None,
    ) -> None:
        super().__init__()
        self._sis = sis
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        self._qml = QQuickWidget(qml_engine, self)  # type: ignore[arg-type]
        self._qml.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        self._qml.setAttribute(Qt.WidgetAttribute.WA_AlwaysStackOnTop, False)
        self._qml.setClearColor(QColor(PALETTE["ELEVATE_2"]))
        self._qml.setSource(str(_TIMING_QML))
        layout.addWidget(self._qml)

    async def set_data(self, pid: str | None, active_pids: list[str]) -> None:
        root_obj = self._qml.rootObject()
        if not root_obj:
            return

        sis = self._sis
        if not sis:
            root_obj.setProperty("sourceData", None)
            return

        report, base, totals, own = await sis.snapshot(active_pids, pid)
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
        metrics_entries, off_thread_entries = self._build_metrics(mod)
        donut_legend = self._build_donut_legend(mod, own, other, base, mod_count)

        base_metrics = self._build_base_metrics(report)

        data = {
            "segments": segments,
            "total_time": _fmt(total),
            "estimated_total": _fmt(total - own if not is_active else total),
            "own_impact": _fmt(own),
            "base_time": _fmt(base),
            "other_time": _fmt(other),
            "base_metrics": base_metrics,
            "has_base_metrics": bool(base_metrics),
            "metrics": metrics_entries,
            "off_thread_metrics": off_thread_entries,
            "has_metrics": bool(metrics_entries or off_thread_entries),
            "donut_bg": base / total if total > 0 else 0,
            "donut_own": own / total if total > 0 else 0,
            "donut_other": other / total if total > 0 else 0,
            "own_color": _impact_color(own),
            "other_color": _COLOR_BASE,
            "bg_color": _COLOR_BASE_GAME,
            "donut_legend": donut_legend,
            "top5": top5_data,
            "top5_label": "Top 5 slowest mods",
            "mod_name": mod.mod_name if mod else None,
            "timestamp": report.timestamp or "",
        }
        root_obj.setProperty("sourceData", data)

    def _build_segments(
        self, mod: StartupImpactMod | None, own: float
    ) -> list[dict]:
        if not mod:
            return []
        denom = max(own, 0.001)
        sorted_m = sorted(mod.metrics.items(), key=lambda x: x[1], reverse=True)
        return [
            {
                "label": metric_label(name),
                "value": _fmt(val_s),
                "fraction": val_s / denom,
                "color": _metric_color(i),
            }
            for i, (name, val_s) in enumerate(sorted_m)
        ]

    def _build_top5(
        self, report: StartupImpactReport, mod: StartupImpactMod | None, own: float
    ) -> list[dict]:
        sorted_mods = sorted(
            report.mods, key=lambda m: m.total_impact_s, reverse=True
        )
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
        max_impact = top5[0][0].total_impact_s if top5 else 1
        result = []
        for entry, is_cur in top5:
            val = own if is_cur else entry.total_impact_s
            bar_color = _COLOR_ACCENT if is_cur else _impact_color(
                entry.total_impact_s
            )
            result.append({
                "label": entry.mod_name,
                "value": _fmt(val),
                "fraction": val / max_impact if max_impact > 0 else 0,
                "is_current": is_cur,
                "color": bar_color,
            })
        return result

    def _build_base_metrics(
        self, report: StartupImpactReport
    ) -> list[dict]:
        sorted_m = sorted(
            report.metrics.items(), key=lambda x: x[1], reverse=True
        )
        return [
            {
                "label": metric_label(name),
                "value": _fmt(val_s),
                "color": _metric_color(i),
            }
            for i, (name, val_s) in enumerate(sorted_m)
        ]

    def _build_metrics(
        self, mod: StartupImpactMod | None
    ) -> tuple[list[dict], list[dict]]:
        if not mod:
            return [], []
        sorted_m = sorted(mod.metrics.items(), key=lambda x: x[1], reverse=True)
        metrics = [
            {
                "label": metric_label(name),
                "value": _fmt(val_s),
                "color": _metric_color(i),
            }
            for i, (name, val_s) in enumerate(sorted_m)
        ]
        sorted_ot = sorted(
            mod.off_thread_metrics.items(), key=lambda x: x[1], reverse=True
        )
        off_thread = [
            {
                "label": metric_label(name),
                "value": _fmt(val_s),
                "color": _metric_color(i),
            }
            for i, (name, val_s) in enumerate(sorted_ot)
        ]
        return metrics, off_thread

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
            legend.append({
                "label": mod.mod_name if mod else "This mod",
                "value": _fmt(own),
                "color": _impact_color(own),
            })
        if other > 0.001:
            legend.append({
                "label": f"Other mods ({mod_count})",
                "value": _fmt(other),
                "color": _COLOR_BASE,
            })
        if base > 0.001:
            legend.append({
                "label": "Base game",
                "value": _fmt(base),
                "color": _COLOR_BASE_GAME,
            })
        return legend

    def clear(self) -> None:
        root_obj = self._qml.rootObject()
        if root_obj:
            root_obj.setProperty("sourceData", None)
