from __future__ import annotations

import asyncio
from typing import TYPE_CHECKING

from pxmodrim.checker.sort import topological_sort
from pxmodrim.core.context import CoreContext
from pxmodrim.models.metadata.structures import AboutXmlMod
from pxmodrim.sort.models import PackageId

if TYPE_CHECKING:
    from pxmodrim.services.diagnostics_service import DiagnosticsService


class SortService:
    def __init__(self, ctx: CoreContext, diagnostics_service: DiagnosticsService):
        """Initialise with core context and diagnostics for topological sort."""
        self._ctx = ctx
        self._diagnostics_service = diagnostics_service

    async def sort_active_mods(self) -> list[str]:
        """Run topological sort on active mods in a thread, returning ordered UUIDs."""
        active_mods = self._diagnostics_service.active_mods_by_pid
        graph = self._diagnostics_service.constraint_graph
        settings = self._ctx.config.sort
        community_rules = self._diagnostics_service.community_rules

        def _sort() -> list[PackageId]:
            return topological_sort(active_mods, graph, settings, community_rules)

        sorted_pids = await asyncio.to_thread(_sort)

        pid_to_uuid = {
            PackageId(mod.package_id): uuid
            for uuid, mod in self._ctx.all_mods.items()
            if isinstance(mod, AboutXmlMod)
        }

        return [pid_to_uuid[pid] for pid in sorted_pids if pid in pid_to_uuid]


