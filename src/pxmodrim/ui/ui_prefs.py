from __future__ import annotations

import msgspec

from pxmodrim.core.constants import LaunchStrategy


def clamp_ui_scale(value: object) -> int:
    if type(value) is not int:
        return 100
    return max(100, min(250, value))


def scale_factor_env(ui_scale: int) -> str | None:
    scale = clamp_ui_scale(ui_scale)
    return f"{scale / 100:g}" if scale != 100 else None


class UIPrefs(msgspec.Struct):
    desc_expanded: bool = False
    launch_strategy: LaunchStrategy = LaunchStrategy.DIRECT
    skipped_update_tag: str = ""
    rail_collapsed: bool = False
    ui_scale: int = 100

    def __post_init__(self) -> None:
        self.ui_scale = clamp_ui_scale(self.ui_scale)
