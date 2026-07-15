from __future__ import annotations

import asyncio
from typing import TYPE_CHECKING

from pxmodrim.core.checker.sort import topological_sort
from pxmodrim.core.context import CoreContext
from pxmodrim.core.models.metadata.structures import AboutXmlMod
from pxmodrim.core.profiler import profile
from pxmodrim.core.sort.models import PackageId

if TYPE_CHECKING:
    from pxmodrim.core.services.diagnostics_service import DiagnosticsService


class SortService:
    def __init__(self, ctx: CoreContext, diagnostics_service: DiagnosticsService):
        """Initialise with core context and diagnostics for topological sort."""
        self._ctx = ctx
        self._diagnostics_service = diagnostics_service

    async def sort_active_mods(self) -> list[str]:
        """Run topological sort on active mods in a thread, returning ordered UUIDs."""
        with profile("sort") as t:
            active_mods = self._diagnostics_service.active_mods_by_pid
            graph = self._diagnostics_service.constraint_graph
            settings = self._ctx.config.sort
            community_rules = self._diagnostics_service.community_rules

            def _sort() -> list[PackageId]:
                return topological_sort(active_mods, graph, settings, community_rules, timer=t)

            sorted_pids = await asyncio.to_thread(_sort)

            pid_to_uuid = {
                PackageId(mod.package_id): uuid
                for uuid, mod in self._ctx.all_mods.items()
                if isinstance(mod, AboutXmlMod)
            }

            return [pid_to_uuid[pid] for pid in sorted_pids if pid in pid_to_uuid]

    def resolve_missing_dependencies(self, active_uuids: set[str]) -> list[str]:
        """Find dependencies of active mods that exist as inactive mods.

        Returns UUIDs of inactive mods that should be enabled to satisfy
        missing dependencies of the currently active set.
        """
        pid_to_uuid: dict[str, str] = {}
        for uuid, mod in self._ctx.all_mods.items():
            if isinstance(mod, AboutXmlMod):
                pid_to_uuid[str(mod.package_id)] = uuid

        active_pids: set[str] = set()
        for uuid in active_uuids:
            mod = self._ctx.all_mods.get(uuid)
            if isinstance(mod, AboutXmlMod):
                active_pids.add(str(mod.package_id))

        deps_to_enable: list[str] = []
        for uuid in active_uuids:
            mod = self._ctx.all_mods.get(uuid)
            if not isinstance(mod, AboutXmlMod):
                continue
            for dep_pid, dep_info in mod.overall_rules.dependencies.items():
                dep_str = str(dep_pid)
                if dep_str in active_pids:
                    continue
                satisfied = any(
                    str(alt) in active_pids
                    for alt in dep_info.alternative_package_ids
                )
                if satisfied:
                    continue
                dep_uuid = pid_to_uuid.get(dep_str)
                if dep_uuid is not None and dep_uuid not in active_uuids:
                    deps_to_enable.append(dep_uuid)

        return list(dict.fromkeys(deps_to_enable))
