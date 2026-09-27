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

    def apply(self, enable: Iterable[str] = (), disable: Iterable[str] = ()) -> bool:
        """Apply batch enable/disable changes; return whether active state changed."""
        active = self._ctx.active_uuids
        disable_set = set(disable)
        all_mods = self._ctx.all_mods
        active_set = set(active)

        new_active = [uuid for uuid in active if uuid not in disable_set]

        seen: set[str] = set()
        for uuid in enable:
            if uuid in seen or uuid in disable_set:
                continue
            seen.add(uuid)
            if uuid in all_mods and uuid not in active_set:
                new_active.append(uuid)

        if new_active == active:
            return False

        self._ctx.set_active(new_active)
        return True
