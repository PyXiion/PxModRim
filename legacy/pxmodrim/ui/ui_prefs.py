from __future__ import annotations

import msgspec

from pxmodrim.core.constants import LaunchStrategy


class UIPrefs(msgspec.Struct):
    desc_expanded: bool = False
    launch_strategy: LaunchStrategy = LaunchStrategy.DIRECT
    skipped_update_tag: str = ""
    rail_collapsed: bool = False
