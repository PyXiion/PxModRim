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
        self._ctx = ctx
        self._diagnostics_service = diagnostics_service

    async def sort_active_mods(self) -> list[str]:
        active_mods = self._diagnostics_service.active_mods_by_pid
        graph = self._diagnostics_service.constraint_graph
        settings = self._ctx.config.sort
        community_rules = self._diagnostics_service.community_rules

        def _sort() -> list[PackageId]:
            pids = topological_sort(active_mods, graph, settings, community_rules)
            return pids

        sorted_pids = await asyncio.to_thread(_sort)

        pid_to_uuid = {
            PackageId(mod.package_id): uuid
            for uuid, mod in self._ctx.all_mods.items()
            if isinstance(mod, AboutXmlMod)
        }

        return [pid_to_uuid[pid] for pid in sorted_pids if pid in pid_to_uuid]

    async def apply_sort(self, ordered_uuids: list[str]) -> bool:
        model = self._ctx.mod_list_model
        if model:
            model.reorder(ordered_uuids)
        return True
