from __future__ import annotations

import pytest

from pxmodrim.core.services.startup_impact_service.models import (
    BASE_GAME_PID,
    StartupImpactMod,
    StartupImpactReport,
)
from pxmodrim.ui.models.impact import format_duration
from pxmodrim.ui.panels.startup_impact_data import (
    BASE_GAME_LABEL,
    build_startup_impact,
)

_P = "LoadingProgress.StartupImpact."


def _report() -> StartupImpactReport:
    return StartupImpactReport(
        path="",
        loading_time_s=0.0,
        timestamp="",
        mods=(
            StartupImpactMod("Small", "a.small", 1.0, {f"{_P}ApplyPatches": 1.0}),
            StartupImpactMod(
                "Big",
                "b.big",
                4.0,
                {
                    f"{_P}ModContentPackReloadContentInt.Textures": 3.0,
                    f"{_P}ApplyPatches": 1.0,
                },
                off_thread_total_impact_s=0.5,
            ),
            StartupImpactMod(
                BASE_GAME_PID,
                BASE_GAME_PID,
                10.0,
                {f"{_P}GlobalTextureAtlasManagerBakeStaticAtlases": 10.0},
            ),
        ),
    )


def _build(**kwargs: object) -> dict:
    args: dict = {"active_pids": [], "selected_pid": None, "estimated_s": 0.0}
    args.update(kwargs)
    return build_startup_impact(_report(), **args)


def test_mods_exclude_base_game_and_rank_by_time() -> None:
    data = _build()
    assert [m["name"] for m in data["mods"]] == ["Big", "Small"]
    assert [m["rank"] for m in data["mods"]] == [1, 2]


def test_segments_scale_to_slowest_mod() -> None:
    big, small = _build()["mods"]
    assert sum(s["fraction"] for s in big["segments"]) == pytest.approx(1.0)
    assert sum(s["fraction"] for s in small["segments"]) == pytest.approx(0.25)
    assert big["dominant"] == "Textures & audio"
    assert big["off_thread"] == "500 ms"


def test_active_and_selected_match_normalized_package_ids() -> None:
    data = _build(active_pids=["B.Big_steam"], selected_pid="A.SMALL")
    flags = {m["name"]: (m["active"], m["selected"]) for m in data["mods"]}
    assert flags == {"Big": (True, False), "Small": (False, True)}
    assert data["active_count"] == 1
    assert data["selected_key"] == "a.small"


def test_estimate_hidden_without_active_list() -> None:
    assert _build(estimated_s=12.0)["estimated"] == ""
    assert _build(active_pids=["a.small"], estimated_s=12.0)["estimated"] == "12.00 s"


def test_last_launch_falls_back_to_measured_sum() -> None:
    assert _build()["last_launch"] == "15.00 s"


def test_summary_splits_base_mods_off_thread() -> None:
    summary = _build()["summary"]
    assert [s["parallel"] for s in summary] == [False, False, True]
    assert sum(s["fraction"] for s in summary) == pytest.approx(1.0)
    assert summary[0]["fraction"] == pytest.approx(10.0 / 15.5)


def test_phases_toggle_base_game_contribution() -> None:
    data = _build(selected_pid="b.big")
    with_base = data["phases_with_base"]
    assert [p["label"] for p in with_base] == ["Textures & audio", "Patches"]
    content = with_base[0]
    assert content["value"] == "13.00 s"
    assert content["share"] == "87%"
    assert [(m["name"], m["base"]) for m in content["mods"]] == [
        (BASE_GAME_LABEL, True),
        ("Big", False),
    ]
    assert content["mods"][1]["selected"]

    mods_only = data["phases_mods_only"]
    assert [(p["label"], p["value"]) for p in mods_only] == [
        ("Textures & audio", "3.00 s"),
        ("Patches", "2.00 s"),
    ]
    assert all(not m["base"] for p in mods_only for m in p["mods"])


@pytest.mark.parametrize(
    ("seconds", "text"),
    [
        (0.0004, "0 ms"),
        (0.25, "250 ms"),
        (59.5, "59.50 s"),
        (60.0, "1m 00s"),
        (268.7, "4m 29s"),
    ],
)
def test_format_duration(seconds: float, text: str) -> None:
    assert format_duration(seconds) == text


def test_tooltip_sums_metrics_sharing_a_label() -> None:
    from pxmodrim.core.services.startup_impact_service.phases import Phase
    from pxmodrim.ui.panels.startup_impact_data import _metrics_by_phase

    grouped = _metrics_by_phase(
        {"ApplyPatches|Foo -> a": 1.0, "ApplyPatches|Foo -> b": 2.0}
    )
    assert list(grouped[Phase.PATCHES].values()) == [3.0]
