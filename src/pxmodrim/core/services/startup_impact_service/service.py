from __future__ import annotations

from pathlib import Path

from loguru import logger

from pxmodrim.core.context import CoreContext
from pxmodrim.core.services.startup_impact_service.models import (
    StartupImpactReport,
)
from pxmodrim.core.services.startup_impact_service.parser import (
    get_startup_impact_path,
    parse_startup_impact,
)


class StartupImpactService:
    """Loads and exposes RimWorld startup impact report data."""

    __slots__ = ("_ctx", "_report")

    def __init__(self, ctx: CoreContext) -> None:
        self._ctx = ctx
        self._report: StartupImpactReport | None = None

    @property
    def report(self) -> StartupImpactReport | None:
        return self._report

    async def load(self) -> StartupImpactReport | None:
        cfg = self._ctx.config
        if not cfg.paths.config_folder:
            logger.debug("No config folder set, skipping startup impact")
            return None

        xml_path = get_startup_impact_path(Path(cfg.paths.config_folder))
        logger.debug("Loading startup impact from {}", xml_path)
        report = parse_startup_impact(xml_path)

        if report and report.mods:
            self._report = report
        else:
            self._report = None

        return self._report

    async def clear(self) -> None:
        self._report = None
