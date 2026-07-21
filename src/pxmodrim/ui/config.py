from __future__ import annotations

from pathlib import Path

import msgspec

from pxmodrim.core.config import config_dir
from pxmodrim.ui.ui_prefs import UIPrefs

__all__ = ["UIPrefs", "load_ui_prefs", "save_ui_prefs"]


def _ui_prefs_path() -> Path:
    return config_dir() / "ui_prefs.json"


def load_ui_prefs(path: Path | None = None) -> UIPrefs:
    """Load UI prefs from the dedicated ui_prefs.json file."""
    path = path or _ui_prefs_path()
    if not path.exists():
        return UIPrefs()
    try:
        return msgspec.json.decode(path.read_bytes(), type=UIPrefs)
    except (OSError, msgspec.DecodeError) as e:
        from loguru import logger

        logger.warning(f"Failed to load UI prefs from {path}: {e}")
        return UIPrefs()


def save_ui_prefs(prefs: UIPrefs, path: Path | None = None) -> None:
    """Serialize and write UI prefs to the dedicated ui_prefs.json file."""
    from loguru import logger

    path = path or _ui_prefs_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    formatted = msgspec.json.format(msgspec.json.encode(prefs), indent=2)
    path.write_bytes(formatted)
    logger.info(f"UI prefs saved to {path}")
