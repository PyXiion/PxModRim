from __future__ import annotations

import os
import tempfile
from datetime import UTC, datetime
from pathlib import Path

import lxml.etree as ET
from loguru import logger

from pxmodrim.core.constants import RIMWORLD_DLC_METADATA
from pxmodrim.core.models.metadata.structures import CaseInsensitiveStr, ModsConfig
from pxmodrim.core.xml import dict_to_etree


def _get_dlc_package_ids() -> list[CaseInsensitiveStr]:
    return [
        CaseInsensitiveStr(dlc["packageid"]) for dlc in RIMWORLD_DLC_METADATA.values()
    ]


def _parse_mods_config(data: bytes, source: Path) -> ModsConfig | None:
    try:
        root = ET.fromstring(data)
    except ET.XMLSyntaxError as e:
        logger.error(f"Failed to parse ModsConfig.xml at {source}: {e}")
        return None

    version_node = root.find("version")
    active_mods_node = root.find("activeMods")
    if root.tag != "ModsConfigData" or version_node is None or active_mods_node is None:
        logger.error(f"Invalid ModsConfig.xml structure at {source}")
        return None

    version = (version_node.text or "1.5").strip()
    active_mods = [
        CaseInsensitiveStr(text)
        for element in active_mods_node.findall("li")
        if (text := (element.text or "").strip())
    ]
    known_expansions = [
        CaseInsensitiveStr(text)
        for element in root.findall("./knownExpansions/li")
        if (text := (element.text or "").strip())
    ]
    if not known_expansions:
        known_expansions = _get_dlc_package_ids()

    return ModsConfig(
        version=version,
        activeMods=active_mods,
        knownExpansions=known_expansions,
    )


def parse_mods_config(path: Path) -> ModsConfig | None:
    """Parse a ModsConfig.xml file into a structured ModsConfig object."""
    try:
        data = path.read_bytes()
    except OSError as e:
        logger.warning(f"Failed to read ModsConfig.xml at {path}: {e}")
        return None
    return _parse_mods_config(data, path)


def _snapshot_files(snapshots_dir: Path) -> list[Path]:
    snapshots = [
        path for path in snapshots_dir.glob("ModsConfig_*.xml") if path.is_file()
    ]
    snapshots.sort(key=lambda path: (path.stat().st_mtime_ns, path.name), reverse=True)
    return snapshots


def _atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temp_name = tempfile.mkstemp(
        dir=path.parent,
        prefix=f".{path.name}.",
        suffix=".tmp",
    )
    tmp_path = Path(temp_name)
    try:
        with os.fdopen(descriptor, "wb") as tmp:
            tmp.write(data)
            tmp.flush()
            os.fsync(tmp.fileno())
        os.replace(tmp_path, path)
    finally:
        tmp_path.unlink(missing_ok=True)


def _prune_snapshots(
    snapshots_dir: Path,
    max_snapshots: int,
    preserved_path: Path | None = None,
) -> None:
    snapshots = _snapshot_files(snapshots_dir)
    excess = len(snapshots) - max(1, max_snapshots)
    removable = [path for path in reversed(snapshots) if path != preserved_path]
    for path in removable[: max(0, excess)]:
        try:
            path.unlink()
        except OSError as e:
            logger.warning(f"Failed to remove old snapshot {path}: {e}")


def create_snapshot(
    config_path: Path,
    snapshots_dir: Path,
    max_snapshots: int = 10,
) -> Path | None:
    """Preserve a uniquely named snapshot of an existing ModsConfig.xml file."""
    if not config_path.is_file():
        return None

    try:
        config_data = config_path.read_bytes()
    except OSError as e:
        logger.warning(f"Failed to read {config_path} for snapshot: {e}")
        return None

    snapshots = get_snapshots(snapshots_dir)
    if snapshots:
        try:
            if snapshots[0].read_bytes() == config_data:
                _prune_snapshots(snapshots_dir, max_snapshots)
                return snapshots[0]
        except OSError:
            pass

    timestamp = datetime.now(UTC).strftime("%Y%m%d_%H%M%S_%f")
    snapshot_path = snapshots_dir / f"ModsConfig_{timestamp}.xml"
    counter = 1
    while snapshot_path.exists():
        snapshot_path = snapshots_dir / f"ModsConfig_{timestamp}_{counter:03d}.xml"
        counter += 1

    _atomic_write(snapshot_path, config_data)
    _prune_snapshots(snapshots_dir, max_snapshots)
    logger.info(f"Snapshot created at {snapshot_path}")
    return snapshot_path


def get_snapshots(snapshots_dir: Path) -> list[Path]:
    """Return available snapshot files, sorted newest first."""
    if not snapshots_dir.exists():
        return []
    return _snapshot_files(snapshots_dir)


def restore_snapshot(
    snapshot_path: Path,
    target_path: Path,
    snapshots_dir: Path,
    max_snapshots: int = 10,
) -> bool:
    """Atomically restore a valid snapshot while preserving the current file."""
    try:
        snapshot_data = snapshot_path.read_bytes()
    except OSError as e:
        logger.error(f"Failed to read snapshot {snapshot_path}: {e}")
        return False

    if _parse_mods_config(snapshot_data, snapshot_path) is None:
        logger.error(f"Snapshot is invalid or corrupted: {snapshot_path}")
        return False

    if target_path.exists():
        limit = max(1, max_snapshots)
        create_snapshot(target_path, snapshots_dir, max_snapshots=limit + 1)
        _prune_snapshots(snapshots_dir, limit, preserved_path=snapshot_path)
    _atomic_write(target_path, snapshot_data)
    logger.info(f"Restored snapshot {snapshot_path} to {target_path}")
    return True


def write_mods_config(
    path: Path,
    data: ModsConfig,
    snapshots_dir: Path,
    max_snapshots: int = 10,
) -> None:
    root: ET._Element | None = None
    if path.exists():
        try:
            existing_root = ET.parse(str(path)).getroot()
        except (OSError, ET.XMLSyntaxError):
            existing_root = None
        if existing_root is not None and existing_root.tag == "ModsConfigData":
            root = existing_root

    if root is None:
        root = dict_to_etree({"ModsConfigData": data.to_dict()})
    else:
        fields = data.to_dict()
        for name in ("version", "activeMods", "knownExpansions"):
            node = root.find(name)
            if node is None:
                node = ET.SubElement(root, name)
            value = fields[name]
            if name == "version":
                node.text = str(value)
                continue
            for child in node.findall("li"):
                node.remove(child)
            for item in value["li"]:
                ET.SubElement(node, "li").text = item

    formatted = ET.tostring(
        root, encoding="utf-8", xml_declaration=True, pretty_print=True
    )
    if path.exists():
        create_snapshot(path, snapshots_dir, max_snapshots=max_snapshots)
    _atomic_write(path, formatted)
    logger.info(f"ModsConfig.xml written to {path}")
