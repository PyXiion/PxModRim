from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator
from heapq import heapify, heappop, heappush

from pxmodrim.core.checker.graph import (
    ConstraintEdge,
    ConstraintGraph,
    EdgeType,
    PackageId,
)
from pxmodrim.core.context import CoreContext
from pxmodrim.core.models.metadata.structures import AboutXmlMod, ListedMod


class ActivationService:
    """Enable/disable mods without reordering the rest of the active list."""

    __slots__ = ("_ctx",)

    def __init__(self, ctx: CoreContext) -> None:
        self._ctx = ctx

    def dependents_of(
        self, uuids: Iterable[str], *, enable: Iterable[str] = ()
    ) -> list[str]:
        """Active dependents of packages lost after the enable/disable changes."""
        excluded = set(uuids)
        all_mods = self._ctx.all_mods
        remaining = {
            PackageId(mod.package_id)
            for uuid in (*self._ctx.active_uuids, *enable)
            if uuid not in excluded
            and isinstance(mod := all_mods.get(uuid), AboutXmlMod)
        }
        roots: set[PackageId] = set()
        for uuid in excluded:
            mod = all_mods.get(uuid)
            if (
                isinstance(mod, AboutXmlMod)
                and PackageId(mod.package_id) not in remaining
            ):
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
        new_active = [uuid for uuid in active if uuid not in disable_set]
        candidates = self._enable_candidates(enable, disable_set, set(active))

        placement = _Placement(
            new_active, candidates, self._ctx.all_mods, self._build_edges
        )
        result = placement.assemble()
        if result == active:
            return False

        self._ctx.set_active(result)
        return True

    def _enable_candidates(
        self, enable: Iterable[str], disable_set: set[str], active_set: set[str]
    ) -> list[str]:
        all_mods = self._ctx.all_mods
        candidates: list[str] = []
        seen: set[str] = set()
        for uuid in enable:
            if uuid in seen or uuid in disable_set:
                continue
            seen.add(uuid)
            if uuid in all_mods and uuid not in active_set:
                candidates.append(uuid)
        return candidates

    def _build_edges(
        self,
        mods: dict[PackageId, AboutXmlMod],
        order: list[PackageId],
        candidates: Iterable[PackageId],
    ) -> list[tuple[PackageId, PackageId]]:
        """(before, after) pairs between *mods* touching any of *candidates*.

        Edges through ids outside *mods* (uninstalled optional mods) are
        dropped so they cannot chain unrelated mods together.
        """
        graph = ConstraintGraph()
        graph.build(
            mods,
            order,
            self._ctx.config.sort,
            self._ctx.diagnostics_service.community_rules,
        )
        # Placement ignores active-to-active edges, so only edges incident to a
        # candidate matter; the graph still resolves the full alternative map.
        incident: set[ConstraintEdge] = set()
        for pid in candidates:
            incident.update(graph.outgoing(pid))
            incident.update(graph.incoming(pid))
        edges: list[tuple[PackageId, PackageId]] = []
        for edge in incident:
            if edge.type == EdgeType.LOAD_BEFORE:
                before, after = edge.source, edge.target
            elif edge.type in (EdgeType.DEPENDENCY, EdgeType.LOAD_AFTER):
                before, after = edge.target, edge.source
            else:
                continue
            if before != after and before in mods and after in mods:
                edges.append((before, after))
        return edges


class _Placement:
    """Slots enable candidates into gaps of a fixed active order.

    Gap ``i`` means "just before ``new_active[i]``"; gap ``len(new_active)``
    is the end. Bounds come only from direct edges to active mods, propagated
    through batch candidates, because the active order itself may already
    violate constraints and must not be re-derived transitively.
    """

    def __init__(
        self,
        new_active: list[str],
        candidates: list[str],
        all_mods: dict[str, ListedMod],
        build_edges: Callable[
            [dict[PackageId, AboutXmlMod], list[PackageId], Iterable[PackageId]],
            list[tuple[PackageId, PackageId]],
        ],
    ) -> None:
        self._new_active = new_active
        self._candidates = candidates
        self._end = len(new_active)

        mods: dict[PackageId, AboutXmlMod] = {}
        order: list[PackageId] = []
        self._first_pos: dict[PackageId, int] = {}
        self._last_pos: dict[PackageId, int] = {}
        for index, uuid in enumerate(new_active):
            mod = all_mods.get(uuid)
            if isinstance(mod, AboutXmlMod):
                pid = PackageId(mod.package_id)
                self._first_pos.setdefault(pid, index)
                self._last_pos[pid] = index
                if pid not in mods:
                    mods[pid] = mod
                    order.append(pid)

        self._uuids_by_pid: dict[PackageId, list[str]] = {}
        for uuid in candidates:
            mod = all_mods[uuid]
            if not isinstance(mod, AboutXmlMod):
                continue
            pid = PackageId(mod.package_id)
            self._uuids_by_pid.setdefault(pid, []).append(uuid)
            if pid not in mods:
                mods[pid] = mod
                order.append(pid)

        # Batch-internal adjacency plus direct bounds against active mods.
        self._after: dict[str, set[str]] = {uuid: set() for uuid in candidates}
        self._before: dict[str, set[str]] = {uuid: set() for uuid in candidates}
        self._floor: dict[str, int] = {}
        self._ceiling: dict[str, int] = {}
        if self._uuids_by_pid:
            for before, after in build_edges(mods, order, self._uuids_by_pid):
                self._add_edge(before, after)

    def _add_edge(self, before: PackageId, after: PackageId) -> None:
        before_uuids = self._uuids_by_pid.get(before, ())
        after_uuids = self._uuids_by_pid.get(after, ())
        for b in before_uuids:
            for a in after_uuids:
                if a != b:
                    self._after[b].add(a)
                    self._before[a].add(b)
            first = self._first_pos.get(after)
            if first is not None:
                self._ceiling[b] = min(self._ceiling.get(b, first), first)
        last = self._last_pos.get(before)
        if last is not None:
            for a in after_uuids:
                self._floor[a] = max(self._floor.get(a, last + 1), last + 1)

    def assemble(self) -> list[str]:
        ordered, cyclic = self._order_batch()
        ceilings = self._propagated_ceilings(ordered, cyclic)
        buckets: list[list[str]] = [[] for _ in range(self._end + 1)]
        gaps: dict[str, int] = {}
        for uuid in ordered:
            gap = self._gap(uuid, cyclic, ceilings, gaps)
            gaps[uuid] = gap
            buckets[gap].append(uuid)

        result: list[str] = []
        for index, uuid in enumerate(self._new_active):
            result.extend(buckets[index])
            result.append(uuid)
        result.extend(buckets[-1])
        return result

    def _gap(
        self,
        uuid: str,
        cyclic: set[str],
        ceilings: dict[str, int],
        gaps: dict[str, int],
    ) -> int:
        if uuid in cyclic:
            return self._end
        floors = [gaps[b] for b in self._before[uuid]]
        if uuid in self._floor:
            floors.append(self._floor[uuid])
        ceiling = ceilings.get(uuid)
        if floors:
            earliest = max(floors)
            if ceiling is not None and earliest > ceiling:
                return self._end
            return earliest
        return self._end if ceiling is None else ceiling

    def _propagated_ceilings(
        self, ordered: list[str], cyclic: set[str]
    ) -> dict[str, int]:
        """Earliest active position each candidate must precede, via batch chains."""
        ceilings: dict[str, int] = {}
        for uuid in reversed(ordered):
            if uuid in cyclic:
                continue
            bounds = [ceilings[a] for a in self._after[uuid] if a in ceilings]
            if uuid in self._ceiling:
                bounds.append(self._ceiling[uuid])
            if bounds:
                ceilings[uuid] = min(bounds)
        return ceilings

    def _order_batch(self) -> tuple[list[str], set[str]]:
        """Topologically order candidates by input order; cycle members as a unit."""
        components = _strongly_connected(self._candidates, self._after)
        component_of = {
            uuid: index for index, members in enumerate(components) for uuid in members
        }
        cyclic = {
            uuid for members in components if len(members) > 1 for uuid in members
        }

        input_index = {uuid: index for index, uuid in enumerate(self._candidates)}
        successors: list[set[int]] = [set() for _ in components]
        indegree = [0] * len(components)
        for uuid, afters in self._after.items():
            source = component_of[uuid]
            for after in afters:
                target = component_of[after]
                if target != source and target not in successors[source]:
                    successors[source].add(target)
                    indegree[target] += 1

        for members in components:
            members.sort(key=input_index.__getitem__)
        ready = [
            (input_index[members[0]], index)
            for index, members in enumerate(components)
            if indegree[index] == 0
        ]
        heapify(ready)
        ordered: list[str] = []
        while ready:
            _, index = heappop(ready)
            ordered.extend(components[index])
            for target in successors[index]:
                indegree[target] -= 1
                if indegree[target] == 0:
                    heappush(ready, (input_index[components[target][0]], target))
        return ordered, cyclic


def _strongly_connected(
    nodes: list[str], adjacent: dict[str, set[str]]
) -> list[list[str]]:
    """Iterative Tarjan SCC."""
    index_of: dict[str, int] = {}
    lowlink: dict[str, int] = {}
    stack: list[str] = []
    on_stack: set[str] = set()
    components: list[list[str]] = []
    for root in nodes:
        if root in index_of:
            continue
        work: list[tuple[str, Iterator[str]]] = []
        index_of[root] = lowlink[root] = len(index_of)
        stack.append(root)
        on_stack.add(root)
        work.append((root, iter(adjacent[root])))
        while work:
            node, children = work[-1]
            for child in children:
                if child not in index_of:
                    index_of[child] = lowlink[child] = len(index_of)
                    stack.append(child)
                    on_stack.add(child)
                    work.append((child, iter(adjacent[child])))
                    break
                if child in on_stack:
                    lowlink[node] = min(lowlink[node], index_of[child])
            else:
                work.pop()
                if work:
                    parent = work[-1][0]
                    lowlink[parent] = min(lowlink[parent], lowlink[node])
                if lowlink[node] == index_of[node]:
                    component: list[str] = []
                    while True:
                        member = stack.pop()
                        on_stack.discard(member)
                        component.append(member)
                        if member == node:
                            break
                    components.append(component)
    return components
