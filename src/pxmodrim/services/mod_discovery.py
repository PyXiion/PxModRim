from __future__ import annotations

from pathlib import Path

from loguru import logger

from pxmodrim._compat.mods_config import parse_mods_config
from pxmodrim._compat.utils import find_about_xml
from pxmodrim.models.metadata.structures import AboutXmlMod, ListedMod


def scan_mod_directory(mods_path: Path) -> list[Path]:
    if not mods_path.exists() or not mods_path.is_dir():
        logger.warning(f"Mod directory not found: {mods_path}")
        return []

    results: list[Path] = []
    for entry in mods_path.iterdir():
        if not entry.is_dir():
            continue
        if find_about_xml(entry):
            results.append(entry)
    return results


def resolve_active_uuids(
    all_mods: dict[str, ListedMod],
    config_folder: str,
) -> set[str]:
    if not config_folder:
        return set()
    path = Path(config_folder) / "ModsConfig.xml"
    data = parse_mods_config(path)
    if not data:
        return set()

    active_pids = {str(pid).removesuffix("_steam").lower() for pid in data.activeMods}
    active: set[str] = set()
    for uuid, mod in all_mods.items():
        if isinstance(mod, AboutXmlMod) and mod.package_id.lower() in active_pids:
            active.add(uuid)
    return active
