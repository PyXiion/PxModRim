from __future__ import annotations

from pathlib import Path

from loguru import logger

from pxmodrim.core.config import config_dir
from pxmodrim.core.context import CoreContext
from pxmodrim.core.services.startup_impact_service import db
from pxmodrim.core.services.startup_impact_service.models import (
    StartupImpactMod,
    StartupImpactReport,
    normalize_package_id,
)
from pxmodrim.core.services.startup_impact_service.parser import (
    get_startup_impact_path,
    parse_startup_impact,
)

_BASE_GAME_PID = "__base_game__"


class StartupImpactService:
    """Loads startup impact data into DB and provides async queries for
    historical averages and per-session metric breakdowns."""

    __slots__ = ("_ctx",)

    def __init__(self, ctx: CoreContext) -> None:
        self._ctx = ctx

    async def load(self) -> StartupImpactReport | None:
        cfg = self._ctx.config
        if not cfg.paths.config_folder:
            return None

        dbp = db.db_path(config_dir())
        json_path = get_startup_impact_path(Path(cfg.paths.config_folder))
        logger.debug("Loading startup impact from {}", json_path)

        report = parse_startup_impact(json_path)
        if report:
            base = max(0.0, report.base_game_time)
            entries: list[StartupImpactMod] = list(report.mods)
            if base > 0.001:
                entries.append(
                    StartupImpactMod(
                        mod_name="__base_game__",
                        package_id=_BASE_GAME_PID,
                        total_impact_s=base,
                    )
                )
            full = StartupImpactReport(
                path=report.path,
                loading_time_s=report.loading_time_s,
                mods=tuple(entries),
                timestamp=report.timestamp,
            )
            await db.store_report(dbp, full)

        return report

    async def get_latest_report(self) -> StartupImpactReport | None:
        return await db.get_latest_report(db.db_path(config_dir()))

    async def estimated_impact_for(self, package_id: str) -> float:
        return await db.get_average(
            db.db_path(config_dir()),
            normalize_package_id(package_id),
        )

    async def estimated_total(self, active_pids: list[str]) -> float:
        dbp = db.db_path(config_dir())
        bg = await db.get_average(dbp, _BASE_GAME_PID)
        totals = await db.get_totals_for(dbp, active_pids)
        mod_sum = sum(on + off for on, off in totals.values())
        return bg + mod_sum

    async def get_all_averages(self) -> dict[str, float]:
        return await db.get_all_averages(db.db_path(config_dir()))

    async def base_game_average(self) -> float:
        return await db.get_average(db.db_path(config_dir()), _BASE_GAME_PID)

    async def snapshot(
        self, active_pids: list[str], selected_pid: str | None = None
    ) -> tuple[
        StartupImpactReport | None,
        float,
        dict[str, tuple[float, float]],
        float,
    ]:
        """Single round-trip fetch of the latest report plus base-game average,
        per-mod on/off-thread averages, and the selected mod's average. Replaces
        the four sequential queries previously issued on every mod toggle.
        """
        return await db.get_latest_with_averages(
            db.db_path(config_dir()),
            active_pids,
            normalize_package_id(selected_pid) if selected_pid else None,
        )

    async def clear(self) -> None:
        await db.clear(db.db_path(config_dir()))

    async def close_connection(self) -> None:
        await db.close()
