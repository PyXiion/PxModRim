from __future__ import annotations

import pytest

from pxmodrim.core.services.startup_impact_service.labels import metric_label
from pxmodrim.core.services.startup_impact_service.phases import (
    Phase,
    metric_phase,
    phase_totals,
)

_P = "LoadingProgress.StartupImpact."


@pytest.mark.parametrize(
    ("key", "phase"),
    [
        (f"{_P}ModContentPackReloadContentInt.Textures", Phase.CONTENT),
        (f"{_P}GlobalTextureAtlasManagerBakeStaticAtlases", Phase.CONTENT),
        (f"{_P}ModConstructor", Phase.MOD_CONSTRUCTORS),
        (f"{_P}StaticConstructorOnStartupUtilityCallAll", Phase.STATIC_CONSTRUCTORS),
        (f"{_P}ApplyPatches", Phase.PATCHES),
        (f"{_P}ErrorCheckPatches", Phase.PATCHES),
        (f"{_P}LoadDefs", Phase.DEFS),
        (f"{_P}ResolveAllWantedCrossReferences.NonImplied", Phase.DEFS),
        (
            f"{_P}ExecuteToExecuteWhenFinished|HugsLib.Utils.HarmonyUtility",
            Phase.DEFERRED,
        ),
        (f"{_P}LanguageDatabaseInitAllMetadata", Phase.OTHER),
        ("SomethingNew", Phase.OTHER),
    ],
)
def test_metric_phase(key: str, phase: Phase) -> None:
    assert metric_phase(key) is phase


def test_phase_totals_sums_per_phase_and_zero_fills() -> None:
    totals = phase_totals(
        {f"{_P}ApplyPatches": 1.5, f"{_P}LoadPatches": 0.5, f"{_P}LoadDefs": 2.0}
    )
    assert totals[Phase.PATCHES] == pytest.approx(2.0)
    assert totals[Phase.DEFS] == pytest.approx(2.0)
    assert totals[Phase.CONTENT] == 0.0
    assert set(totals) == set(Phase)


def test_metric_label_names_deferred_task_target() -> None:
    key = (
        f"{_P}ExecuteToExecuteWhenFinished|RimWorld.AbilityDef"
        " -> Void <PostLoad>b__88_0()"
    )
    expected = "Running delayed initialization task (RimWorld.AbilityDef)"
    assert metric_label(key) == expected
