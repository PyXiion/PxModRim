from __future__ import annotations

from typing import TYPE_CHECKING

from pxmodrim.ui.ui_prefs import UIPrefs

if TYPE_CHECKING:
    from pxmodrim.core.config import ConfigService

__all__ = ["UIPrefs", "load_ui_prefs", "save_ui_prefs"]


def load_ui_prefs(config_service: ConfigService) -> UIPrefs:
    """Load UI prefs from ``ui_prefs.json`` via config service."""
    return config_service.load("ui_prefs.json", UIPrefs)


def save_ui_prefs(prefs: UIPrefs, config_service: ConfigService) -> None:
    """Save UI prefs to ``ui_prefs.json`` via config service."""
    config_service.save("ui_prefs.json", prefs)
