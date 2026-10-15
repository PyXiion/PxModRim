from __future__ import annotations

import asyncio
from pathlib import Path

import pytest

from pxmodrim.core.services.startup_impact_service.db import StartupImpactDb
from pxmodrim.core.services.startup_impact_service.models import (
    BASE_GAME_PID,
    StartupImpactMod,
    StartupImpactReport,
)


@pytest.mark.asyncio
async def test_store_report_and_clear_are_serialized(tmp_path: Path) -> None:
    db = StartupImpactDb()
    path = tmp_path / "cache.db"
    report = StartupImpactReport(
        path="",
        loading_time_s=1.0,
        timestamp="now",
        mods=(StartupImpactMod("Example", "author.example", 0.5),),
    )
    try:
        await asyncio.gather(db.store_report(path, report), db.clear(path))
        latest = await db.get_latest_report(path)
        assert latest is None or latest.mods[0].package_id == "author.example"
        await db.store_report(path, report)
        latest = await db.get_latest_report(path)
        assert latest is not None
        assert latest.mods[0].package_id == "author.example"
    finally:
        await db.close()


@pytest.mark.asyncio
async def test_base_game_keeps_sentinel_package_id_after_round_trip(
    tmp_path: Path,
) -> None:
    db = StartupImpactDb()
    path = tmp_path / "cache.db"
    report = StartupImpactReport(
        path="",
        loading_time_s=1.0,
        timestamp="now",
        mods=(
            StartupImpactMod("Example", "author.example", 0.5),
            StartupImpactMod(BASE_GAME_PID, BASE_GAME_PID, 2.0),
        ),
    )
    try:
        await db.store_report(path, report)
        latest = await db.get_latest_report(path)
    finally:
        await db.close()
    assert latest is not None
    by_name = {m.mod_name: m.package_id for m in latest.mods}
    assert by_name[BASE_GAME_PID] == BASE_GAME_PID
