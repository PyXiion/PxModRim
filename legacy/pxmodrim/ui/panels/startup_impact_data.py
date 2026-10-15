from __future__ import annotations

from collections import defaultdict
from datetime import datetime
from typing import Any

from pxmodrim.core.services.startup_impact_service.labels import metric_label
from pxmodrim.core.services.startup_impact_service.models import (
    BASE_GAME_PID,
    StartupImpactMod,
    StartupImpactReport,
    normalize_package_id,
)
from pxmodrim.core.services.startup_impact_service.phases import (
    PHASE_LABELS,
    Phase,
    metric_phase,
    phase_totals,
)
from pxmodrim.ui.models.impact import format_duration, impact_color
from pxmodrim.ui.theme.palette import PALETTE

PHASE_COLORS: dict[Phase, str] = {
    Phase.CONTENT: "#b57bee",
    Phase.MOD_CONSTRUCTORS: "#f06ba8",
    Phase.STATIC_CONSTRUCTORS: "#43b581",
    Phase.PATCHES: "#f0883e",
    Phase.DEFS: "#4fb3e8",
    Phase.DEFERRED: "#e8c547",
    Phase.OTHER: PALETTE["TEXT_DIM"],
}

BASE_GAME_LABEL = "Base game"
_MIN_S = 0.001
_TOOLTIP_METRICS = 8


def build_startup_impact(
    report: StartupImpactReport,
    *,
    active_pids: list[str],
    selected_pid: str | None,
    estimated_s: float,
) -> dict[str, Any]:
    active = {normalize_package_id(p) for p in active_pids}
    selected = normalize_package_id(selected_pid) if selected_pid else None
    base = next((m for m in report.mods if m.package_id == BASE_GAME_PID), None)
    mods = sorted(
        (m for m in report.mods if m.package_id != BASE_GAME_PID),
        key=lambda m: m.total_impact_s,
        reverse=True,
    )

    base_s = base.total_impact_s if base else 0.0
    mods_s = sum(m.total_impact_s for m in mods)
    off_s = sum(m.off_thread_total_impact_s for m in report.mods)
    bar_total = base_s + mods_s + off_s
    max_mod_s = mods[0].total_impact_s if mods else 0.0

    rows = [
        _mod_row(m, rank, max_mod_s, active, selected)
        for rank, m in enumerate(mods, start=1)
    ]

    entries = [(m.mod_name, _key(m), False, m.metrics) for m in mods]
    base_entry = (BASE_GAME_LABEL, BASE_GAME_PID, True, base.metrics if base else {})

    return {
        "timestamp": _format_timestamp(report.timestamp),
        "last_launch": format_duration(report.loading_time_s or base_s + mods_s),
        "estimated": format_duration(estimated_s) if active_pids else "",
        "summary": [
            {
                "label": label,
                "value": format_duration(seconds),
                "fraction": seconds / bar_total if bar_total > 0 else 0.0,
                "color": color,
                "parallel": parallel,
            }
            for label, seconds, color, parallel in (
                (BASE_GAME_LABEL, base_s, PALETTE["TEXT_DIM"], False),
                ("Mods, main thread", mods_s, PALETTE["PRIMARY"], False),
                ("Off-thread (parallel)", off_s, PALETTE["PRIMARY"], True),
            )
            if seconds >= _MIN_S
        ],
        "base_value": format_duration(base_s),
        "mods_value": format_duration(mods_s),
        "off_value": format_duration(off_s),
        "legend": [{"label": PHASE_LABELS[p], "color": PHASE_COLORS[p]} for p in Phase],
        "mods": rows,
        "active_count": sum(1 for r in rows if r["active"]),
        "selected_key": next((r["key"] for r in rows if r["selected"]), ""),
        "phases_with_base": _phase_rows([base_entry, *entries], selected),
        "phases_mods_only": _phase_rows(entries, selected),
    }


def _key(mod: StartupImpactMod) -> str:
    return mod.package_id or mod.mod_name


def _mod_row(
    mod: StartupImpactMod,
    rank: int,
    max_mod_s: float,
    active: set[str],
    selected: str | None,
) -> dict[str, Any]:
    totals = phase_totals(mod.metrics)
    by_phase = _metrics_by_phase(mod.metrics)
    ranked = sorted(
        ((p, s) for p, s in totals.items() if s >= _MIN_S),
        key=lambda ps: ps[1],
        reverse=True,
    )
    top_phase_s = ranked[0][1] if ranked else 0.0
    pid = mod.package_id
    return {
        "key": _key(mod),
        "name": mod.mod_name,
        "rank": rank,
        "seconds": mod.total_impact_s,
        "time": format_duration(mod.total_impact_s),
        "color": impact_color(mod.total_impact_s),
        "active": pid is not None and pid in active,
        "selected": pid is not None and pid == selected,
        "off_thread": format_duration(mod.off_thread_total_impact_s)
        if mod.off_thread_total_impact_s >= _MIN_S
        else "",
        "dominant": PHASE_LABELS[ranked[0][0]] if ranked else "",
        "segments": [
            {
                "color": PHASE_COLORS[p],
                "fraction": totals[p] / max_mod_s if max_mod_s > 0 else 0.0,
            }
            for p in Phase
            if totals[p] >= _MIN_S
        ],
        "phases": [
            {
                "label": PHASE_LABELS[p],
                "color": PHASE_COLORS[p],
                "value": format_duration(s),
                "fraction": s / top_phase_s,
                "tooltip": _tooltip(by_phase[p]),
            }
            for p, s in ranked
        ],
    }


def _phase_rows(
    entries: list[tuple[str, str, bool, dict[str, float]]],
    selected: str | None,
) -> list[dict[str, Any]]:
    per_phase: dict[Phase, list[tuple[float, str, str, bool]]] = defaultdict(list)
    metric_sums: dict[Phase, dict[str, float]] = defaultdict(lambda: defaultdict(float))
    for name, key, is_base, metrics in entries:
        for phase, seconds in phase_totals(metrics).items():
            if seconds >= _MIN_S:
                per_phase[phase].append((seconds, name, key, is_base))
        for metric, seconds in metrics.items():
            label = metric_label(metric.split("|", 1)[0])
            metric_sums[metric_phase(metric)][label] += seconds

    grand = sum(s for items in per_phase.values() for s, *_ in items)
    totals = {p: sum(s for s, *_ in items) for p, items in per_phase.items()}
    top_phase_s = max(totals.values(), default=0.0)
    rows = []
    for phase, seconds in sorted(totals.items(), key=lambda ps: ps[1], reverse=True):
        contributors = sorted(per_phase[phase], reverse=True)
        top_s = contributors[0][0]
        rows.append(
            {
                "label": PHASE_LABELS[phase],
                "color": PHASE_COLORS[phase],
                "seconds": seconds,
                "value": format_duration(seconds),
                "share": f"{round(100 * seconds / grand)}%" if grand > 0 else "",
                "fraction": seconds / top_phase_s,
                "tooltip": _tooltip(metric_sums[phase]),
                "mods": [
                    {
                        "name": name,
                        "key": key,
                        "value": format_duration(s),
                        "fraction": s / top_s,
                        "base": is_base,
                        "selected": selected is not None and key == selected,
                    }
                    for s, name, key, is_base in contributors
                ],
            }
        )
    return rows


def _metrics_by_phase(metrics: dict[str, float]) -> dict[Phase, dict[str, float]]:
    grouped: dict[Phase, dict[str, float]] = defaultdict(lambda: defaultdict(float))
    for key, seconds in metrics.items():
        grouped[metric_phase(key)][metric_label(key)] += seconds
    return grouped


def _tooltip(metrics: dict[str, float]) -> str:
    top = sorted(metrics.items(), key=lambda kv: kv[1], reverse=True)
    lines = [
        f"{label}: {format_duration(s)}"
        for label, s in top[:_TOOLTIP_METRICS]
        if s >= _MIN_S
    ]
    if len(top) > _TOOLTIP_METRICS:
        lines.append(f"+ {len(top) - _TOOLTIP_METRICS} more")
    return "\n".join(lines)


def _format_timestamp(raw: str) -> str:
    if not raw:
        return ""
    try:
        parsed = datetime.fromisoformat(raw)
    except ValueError:
        return raw
    return parsed.astimezone().strftime("%Y-%m-%d %H:%M")
