from __future__ import annotations

import math
import re
from collections.abc import Sequence
from dataclasses import dataclass
from typing import Literal

import msgspec

from pxmodrim.core.constants import LaunchStrategy


def clamp_ui_scale(value: object) -> int:
    if type(value) is float and value.is_integer():
        value = int(value)
    if type(value) is not int:
        return 100
    return max(100, min(250, value))


def scale_factor_env(ui_scale: int) -> str | None:
    scale = clamp_ui_scale(ui_scale)
    return f"{scale / 100:g}" if scale != 100 else None


@dataclass(frozen=True)
class ResolvedScale:
    scale: float
    source: Literal["saved", "cli", "env", "qt-env"]
    warnings: tuple[str, ...] = ()


def ui_scale_cli_value(arguments: Sequence[str]) -> str | None:
    value = None
    for index, argument in enumerate(arguments):
        if argument == "--":
            break
        if argument.startswith("--ui-scale="):
            value = argument.partition("=")[2]
        elif argument == "--ui-scale":
            value = arguments[index + 1] if index + 1 < len(arguments) else ""
    return value


def resolve_ui_scale(
    saved: int,
    cli_value: str | None,
    env_value: str | None,
    qt_scale_factor_env: str | None,
) -> ResolvedScale:
    if qt_scale_factor_env is not None:
        try:
            factor = float(qt_scale_factor_env)
        except ValueError:
            factor = 1.0
        if not math.isfinite(factor) or factor <= 0:
            factor = 1.0
        return ResolvedScale(factor * 100, "qt-env")

    warnings: list[str] = []
    overrides: tuple[tuple[str | None, Literal["cli", "env"], str], ...] = (
        (cli_value, "cli", "--ui-scale"),
        (env_value, "env", "PX_UI_SCALE"),
    )
    for value, source, label in overrides:
        if value is None:
            continue
        text = value.strip()
        if re.fullmatch(r"[+-]?[0-9]+", text):
            digits = text.lstrip("+-").lstrip("0")
            if text.startswith("-"):
                scale = 100
            elif len(digits) > 3:
                scale = 250
            else:
                scale = clamp_ui_scale(int(digits or "0"))
            return ResolvedScale(scale, source, tuple(warnings))
        warnings.append(f"Ignoring invalid {label} value: {value!r}")
    return ResolvedScale(clamp_ui_scale(saved), "saved", tuple(warnings))


class UIPrefs(msgspec.Struct):
    desc_expanded: bool = False
    launch_strategy: LaunchStrategy = LaunchStrategy.DIRECT
    skipped_update_tag: str = ""
    rail_collapsed: bool = False
    ui_scale: int = 100

    def __post_init__(self) -> None:
        self.ui_scale = clamp_ui_scale(self.ui_scale)
