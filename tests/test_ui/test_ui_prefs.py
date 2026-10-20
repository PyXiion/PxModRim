from __future__ import annotations

import msgspec
import pytest

from pxmodrim.ui.ui_prefs import UIPrefs, clamp_ui_scale, scale_factor_env


@pytest.mark.parametrize(
    ("value", "expected", "factor"),
    [
        (None, 100, None),
        ("garbage", 100, None),
        ("150", 100, None),
        (True, 100, None),
        (150.5, 100, None),
        (-50, 100, None),
        (100, 100, None),
        (125, 125, "1.25"),
        (137, 137, "1.37"),
        (150, 150, "1.5"),
        (175, 175, "1.75"),
        (200, 200, "2"),
        (225, 225, "2.25"),
        (250, 250, "2.5"),
        (500, 250, "2.5"),
    ],
)
def test_ui_scale_clamping_and_environment_factor(
    value: object, expected: int, factor: str | None
) -> None:
    scale = clamp_ui_scale(value)
    assert scale == expected
    assert scale_factor_env(scale) == factor


def test_ui_scale_loads_old_preferences_and_clamps_disk_values() -> None:
    assert msgspec.json.decode(b'{"rail_collapsed": true}', type=UIPrefs) == UIPrefs(
        rail_collapsed=True
    )
    assert msgspec.json.decode(b'{"ui_scale": 500}', type=UIPrefs).ui_scale == 250
