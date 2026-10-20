from __future__ import annotations

import msgspec
import pytest

from pxmodrim.ui.ui_prefs import (
    UIPrefs,
    clamp_ui_scale,
    resolve_ui_scale,
    scale_factor_env,
    ui_scale_cli_value,
)


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


@pytest.mark.parametrize(
    ("saved", "cli", "env", "qt_env", "expected", "source", "warnings"),
    [
        (125, None, None, None, 125, "saved", 0),
        (125, None, "150", None, 150, "env", 0),
        (125, "175", "150", None, 175, "cli", 0),
        (125, "175", "150", "1.5", 150, "qt-env", 0),
        (125, "bad", "150", None, 150, "env", 1),
        (125, "bad", "bad", None, 125, "saved", 2),
        (125, None, "bad", None, 125, "saved", 1),
        (125, "1.5", "175", None, 175, "env", 1),
        (125, "", "175", None, 175, "env", 1),
        (125, "--unknown", "175", None, 175, "env", 1),
        (125, "1_75", None, None, 125, "saved", 1),
        (125, "50", None, None, 100, "cli", 0),
        (125, "999", None, None, 250, "cli", 0),
        (125, None, "-1", None, 100, "env", 0),
        (125, None, "999", None, 250, "env", 0),
        (125, "9" * 5000, None, None, 250, "cli", 0),
        (125, None, "-" + "9" * 5000, None, 100, "env", 0),
        (125, "0" * 5000 + "175", None, None, 175, "cli", 0),
        (999, None, None, None, 250, "saved", 0),
        (125, "bad", "bad", "3", 300, "qt-env", 0),
        (125, "175", "150", "", 100, "qt-env", 0),
        (125, "175", "150", "invalid", 100, "qt-env", 0),
        (125, None, None, "nan", 100, "qt-env", 0),
        (125, " +175 ", "150", None, 175, "cli", 0),
    ],
)
def test_startup_scale_resolution(
    saved: int,
    cli: str | None,
    env: str | None,
    qt_env: str | None,
    expected: float,
    source: str,
    warnings: int,
) -> None:
    resolved = resolve_ui_scale(saved, cli, env, qt_env)
    assert (resolved.scale, resolved.source, len(resolved.warnings)) == (
        expected,
        source,
        warnings,
    )


@pytest.mark.parametrize(
    ("arguments", "expected"),
    [
        ([], None),
        (["--ui-scale", "175"], "175"),
        (["--ui-scale=175"], "175"),
        (["-platform", "offscreen", "--ui-scale", "100", "--unknown"], "100"),
        (["--ui-scale"], ""),
        (["--ui-scale="], ""),
        (["--ui-scale", "-platform", "offscreen"], "-platform"),
        (["--ui-scale=125", "--ui-scale", "175"], "175"),
        (["--", "--ui-scale=175"], None),
    ],
)
def test_scale_flag_preserves_other_arguments(
    arguments: list[str], expected: str | None
) -> None:
    original = arguments.copy()
    assert ui_scale_cli_value(arguments) == expected
    assert arguments == original
