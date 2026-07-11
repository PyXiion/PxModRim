from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from loguru import logger

from pxmodrim._compat.xml import xml_path_to_json


@dataclass
class ModsConfigData:
    version: str = "1.5"
    active_mods: list[str] = field(default_factory=list)
    known_expansions: list[str] = field(default_factory=list)


def parse_mods_config(path: Path) -> ModsConfigData | None:
    if not path.exists():
        logger.warning(f"ModsConfig.xml not found at {path}")
        return None

    data = xml_path_to_json(str(path))
    if not data:
        return None

    try:
        root = data.get("ModsConfigData", {})
        version = root.get("version", "1.5")
        raw_active = root.get("activeMods", {}).get("li", [])
        raw_expansions = root.get("knownExpansions", {}).get("li", [])

        if isinstance(raw_active, str):
            raw_active = [raw_active]
        if isinstance(raw_expansions, str):
            raw_expansions = [raw_expansions]

        return ModsConfigData(
            version=str(version),
            active_mods=list(raw_active),
            known_expansions=list(raw_expansions),
        )
    except Exception as e:
        logger.error(f"Failed to parse ModsConfig.xml: {e}")
        return None


def write_mods_config(path: Path, data: ModsConfigData) -> None:
    import lxml.etree as ET

    root = ET.Element("ModsConfigData")

    version_el = ET.SubElement(root, "version")
    version_el.text = data.version

    active_mods = ET.SubElement(root, "activeMods")
    for pid in data.active_mods:
        li = ET.SubElement(active_mods, "li")
        li.text = pid

    known = ET.SubElement(root, "knownExpansions")
    for eid in data.known_expansions:
        li = ET.SubElement(known, "li")
        li.text = eid

    tree = ET.ElementTree(root)
    raw = ET.tostring(tree, encoding="utf-8", xml_declaration=True, pretty_print=True)

    import xml.dom.minidom as minidom

    reparsed = minidom.parseString(raw)
    formatted = reparsed.toprettyxml(indent="  ", encoding=None)

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(formatted, encoding="utf-8")
    logger.info(f"ModsConfig.xml written to {path}")
