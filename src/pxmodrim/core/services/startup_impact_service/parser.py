from __future__ import annotations

import json
from pathlib import Path

from loguru import logger

from pxmodrim.core.services.startup_impact_service.models import (
    StartupImpactMod,
    StartupImpactReport,
    normalize_package_id,
)

_STARTUP_IMPACT_FILENAME = "StartupImpactData.json"


def get_startup_impact_path(config_folder: str | Path) -> Path:
    return Path(config_folder).parent / _STARTUP_IMPACT_FILENAME


def parse_startup_impact(path: str | Path) -> StartupImpactReport | None:
    p = Path(path)
    if not p.exists():
        logger.debug("Startup impact report not found at {}", p)
        return None

    try:
        data = json.loads(p.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as e:
        logger.warning("Failed to parse startup impact report at {}: {}", p, e)
        return None

    if not isinstance(data, dict):
        logger.warning("Startup impact report is not a JSON object: {}", p)
        return None

    mods: list[StartupImpactMod] = []
    for entry in data.get("mods") or ():
        if not isinstance(entry, dict):
            continue
        mod_name = entry.get("modName")
        if not isinstance(mod_name, str) or not mod_name:
            continue

        raw_package_id = entry.get("modPackageId")
        package_id = (
            normalize_package_id(raw_package_id)
            if isinstance(raw_package_id, str) and raw_package_id
            else None
        )

        mods.append(
            StartupImpactMod(
                mod_name=mod_name,
                package_id=package_id,
                total_impact_s=_as_seconds(entry.get("totalImpact")),
                metrics=_as_string_float_dict(entry.get("metrics")),
                off_thread_metrics=_as_string_float_dict(
                    entry.get("offThreadMetrics")
                ),
                off_thread_total_impact_s=_as_seconds(
                    entry.get("offThreadTotalImpact")
                ),
            )
        )

    return StartupImpactReport(
        path=str(p),
        loading_time_s=_as_seconds(data.get("loadingTime")),
        mods=tuple(mods),
        timestamp=data.get("timestamp") or "",
        metrics=_as_string_float_dict(data.get("metrics")),
        total_impact=_as_seconds(data.get("totalImpact")),
        off_thread_metrics=_as_string_float_dict(data.get("offThreadMetrics")),
        off_thread_total_impact=_as_seconds(data.get("offThreadTotalImpact")),
    )


def _as_seconds(value: object) -> float:
    if isinstance(value, (int, float)):
        return float(value) / 1000.0
    if isinstance(value, str):
        try:
            return float(value) / 1000.0
        except ValueError:
            return 0.0
    return 0.0


def _as_string_float_dict(value: object) -> dict[str, float]:
    if not isinstance(value, dict):
        return {}
    return {
        str(k): float(v) / 1000.0
        for k, v in value.items()
        if isinstance(v, (int, float))
    }
