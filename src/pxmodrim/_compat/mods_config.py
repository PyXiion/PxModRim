from __future__ import annotations

from pathlib import Path
from typing import Any

from loguru import logger

from pxmodrim._compat.constants import RIMWORLD_DLC_METADATA
from pxmodrim._compat.xml import xml_path_to_json
from pxmodrim.models.metadata.structures import CaseInsensitiveStr, ModsConfig


def _extract_li_list(container: dict[str, Any] | list[Any] | str | None) -> list[str]:
    """Extract list of strings from various XML dict structures for <li> elements."""
    if not container:
        return []
    if isinstance(container, list):
        return [str(x) for x in container if isinstance(x, str)]
    if isinstance(container, str):
        return [container]
    if isinstance(container, dict):
        li = container.get("li")
        if isinstance(li, str):
            return [li]
        if isinstance(li, list):
            return [str(x) for x in li if isinstance(x, str)]
    return []


def _get_dlc_package_ids() -> list[CaseInsensitiveStr]:
    return [
        CaseInsensitiveStr(dlc["packageid"]) for dlc in RIMWORLD_DLC_METADATA.values()
    ]


def parse_mods_config(path: Path) -> ModsConfig | None:
    if not path.exists():
        logger.warning(f"ModsConfig.xml not found at {path}")
        return None

    data = xml_path_to_json(str(path))
    if not data:
        return None

    try:
        root = data.get("ModsConfigData", {})
        version = str(root.get("version", "1.5"))

        raw_active = _extract_li_list(root.get("activeMods", {}))
        raw_expansions = _extract_li_list(root.get("knownExpansions", {}))

        active_mods = [CaseInsensitiveStr(pid) for pid in raw_active]
        known_expansions = [CaseInsensitiveStr(pid) for pid in raw_expansions]

        # Fallback: if knownExpansions is empty, populate from known DLCs
        if not known_expansions:
            known_expansions = _get_dlc_package_ids()

        return ModsConfig(
            version=version,
            activeMods=active_mods,
            knownExpansions=known_expansions,
        )
    except Exception as e:
        logger.error(f"Failed to parse ModsConfig.xml: {e}")
        return None


def write_mods_config(path: Path, data: ModsConfig) -> None:
    from pxmodrim._compat.xml import json_to_xml_write

    json_to_xml_write({"ModsConfigData": data.to_dict()}, str(path), raise_errs=True)
    logger.info(f"ModsConfig.xml written to {path}")
