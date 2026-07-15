from __future__ import annotations

from pathlib import Path

from loguru import logger

from pxmodrim.core.services.startup_impact_service.models import (
    StartupImpactMod,
    StartupImpactReport,
    normalize_package_id,
)
from pxmodrim.core.xml import xml_path_to_json

_STARTUP_IMPACT_FILENAME = "StartupImpactData.xml"


def get_startup_impact_path(config_folder: str | Path) -> Path:
    return Path(config_folder).parent / _STARTUP_IMPACT_FILENAME


def parse_startup_impact(path: str | Path) -> StartupImpactReport | None:
    path_str = str(path)
    raw = xml_path_to_json(path_str)
    if not raw:
        return None

    session = raw.get("StartupImpactSession")
    if not isinstance(session, dict):
        logger.warning("Startup impact report has unexpected structure: {}", path_str)
        return None

    session_data = session.get("sessionData")
    if not isinstance(session_data, dict):
        logger.warning("Startup impact report has no sessionData: {}", path_str)
        return None

    mods: list[StartupImpactMod] = []
    mods_container = session_data.get("mods")
    if isinstance(mods_container, dict):
        li_list = mods_container.get("li")
        if isinstance(li_list, list):
            for entry in li_list:
                _parse_entry(entry, mods)
        elif isinstance(li_list, dict):
            _parse_entry(li_list, mods)

    return StartupImpactReport(
        path=path_str,
        loading_time_s=_as_seconds(session_data.get("loadingTime")),
        mods=tuple(mods),
    )


def _parse_entry(entry: object, mods: list[StartupImpactMod]) -> None:
    if not isinstance(entry, dict):
        return
    mod_name = entry.get("modName")
    if not isinstance(mod_name, str) or not mod_name:
        return
    raw_package_id = entry.get("modPackageId")
    package_id = (
        normalize_package_id(raw_package_id)
        if isinstance(raw_package_id, str) and raw_package_id
        else None
    )
    total_impact_s = _as_seconds(entry.get("totalImpact"))
    mods.append(
        StartupImpactMod(
            mod_name=mod_name,
            package_id=package_id,
            total_impact_s=total_impact_s,
        )
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
