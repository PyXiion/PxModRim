from __future__ import annotations

from collections.abc import Iterable

from pxmodrim.core.checker.graph import EdgeType, PackageId
from pxmodrim.core.context import CoreContext
from pxmodrim.core.models.metadata.structures import AboutXmlMod


class ActivationService:
    """Enable/disable mods without reordering the rest of the active list."""

    __slots__ = ("_ctx",)

    def __init__(self, ctx: CoreContext) -> None:
        self._ctx = ctx

    def dependents_of(self, uuids: Iterable[str]) -> list[str]:
        """Active mods that transitively depend on any of *uuids*, in active order."""
        excluded = set(uuids)
        all_mods = self._ctx.all_mods
        roots: set[PackageId] = set()
        for uuid in excluded:
            mod = all_mods.get(uuid)
            if isinstance(mod, AboutXmlMod):
                roots.add(PackageId(mod.package_id))
        if not roots:
            return []

        graph = self._ctx.diagnostics_service.constraint_graph
        dependents: set[PackageId] = set()
        pending = list(roots)
        while pending:
            target = pending.pop()
            for edge in graph.incoming_of_type(target, EdgeType.DEPENDENCY):
                if edge.source not in roots and edge.source not in dependents:
                    dependents.add(edge.source)
                    pending.append(edge.source)

        result: list[str] = []
        for uuid in self._ctx.active_uuids:
            if uuid in excluded:
                continue
            mod = all_mods.get(uuid)
            if isinstance(mod, AboutXmlMod) and PackageId(mod.package_id) in dependents:
                result.append(uuid)
        return result

    def set_enabled(self, uuids: Iterable[str], enabled: bool) -> bool:
        """Append newly enabled mods or drop disabled ones; return whether changed."""
        active = self._ctx.active_uuids
        active_set = set(active)
        if enabled:
            all_mods = self._ctx.all_mods
            added = [
                uuid
                for uuid in dict.fromkeys(uuids)
                if uuid in all_mods and uuid not in active_set
            ]
            if not added:
                return False
            self._ctx.set_active(active + added)
            return True

        removed = active_set.intersection(uuids)
        if not removed:
            return False
        self._ctx.set_active([uuid for uuid in active if uuid not in removed])
        return True
