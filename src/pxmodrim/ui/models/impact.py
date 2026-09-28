from __future__ import annotations

from pxmodrim.core.services.startup_impact_service.models import (
    IMPACT_HIGH_THRESHOLD_S,
    IMPACT_WARN_THRESHOLD_S,
)
from pxmodrim.ui.theme.palette import PALETTE


def format_duration(seconds: float) -> str:
    if seconds < 0.001:
        return "0 ms"
    if seconds < 1:
        return f"{round(seconds * 1000)} ms"
    return f"{seconds:.2f} s"


def impact_color(seconds: float) -> str:
    if seconds < IMPACT_WARN_THRESHOLD_S:
        return PALETTE["SUCCESS"]
    if seconds < IMPACT_HIGH_THRESHOLD_S:
        return PALETTE["WARNING"]
    return PALETTE["DANGER"]
