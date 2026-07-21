from __future__ import annotations

import msgspec

from pxmodrim.core.constants import LaunchStrategy


class UIPrefs(msgspec.Struct):
    deps_expanded: bool = True
    desc_expanded: bool = False
    launch_strategy: LaunchStrategy = LaunchStrategy.DIRECT
    validate_downloads: bool = False
