from __future__ import annotations

import os
from pathlib import Path

from loguru import logger

from pxmodrim.core.models.metadata.structures import AboutXmlMod, ListedMod
from pxmodrim.core.mods_config import parse_mods_config
from pxmodrim.core.utils import find_about_xml


def scan_mod_directory(mods_path: Path) -> dict[Path, Path]:
    """Scan a directory for mod subdirectories, returning {mod_path: about_xml_path}."""
    if not mods_path.exists() or not mods_path.is_dir():
        logger.warning(f"Mod directory not found: {mods_path}")
        return {}

    results: dict[Path, Path] = {}
    with os.scandir(mods_path) as it:
        for entry in it:
            if not entry.is_dir():
                continue
            p = Path(entry.path)
            about = find_about_xml(p)
            if about:
                results[p] = about
    return results


def resolve_active_uuids(
    all_mods: dict[str, ListedMod],
    config_folder: str,
) -> list[str]:
    """Parse ModsConfig.xml and return ordered UUIDs matching active mods."""
    if not config_folder:
        return []
    path = Path(config_folder) / "ModsConfig.xml"
    data = parse_mods_config(path)
    if not data:
        return []

    # Build map from normalized package_id -> UUID (first match for duplicates)
    pid_to_uuid: dict[str, str] = {}
    for uuid, mod in all_mods.items():
        if isinstance(mod, AboutXmlMod):
            pid = mod.package_id.lower().removesuffix("_steam")
            if pid not in pid_to_uuid:
                pid_to_uuid[pid] = uuid

    # Iterate ModsConfig.xml activeMods IN ORDER, pick matching UUID
    active: list[str] = []
    for pid in data.activeMods:
        normalized = str(pid).removesuffix("_steam").lower()
        if normalized in pid_to_uuid:
            active.append(pid_to_uuid[normalized])
    return active
