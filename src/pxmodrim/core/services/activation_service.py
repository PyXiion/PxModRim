from __future__ import annotations

from collections.abc import Iterable
from heapq import heappop, heappush

from pxmodrim.core.checker.graph import ConstraintGraph, EdgeType, PackageId
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

        enable_candidates: list[str] = []
        seen: set[str] = set()
        for uuid in enable:
            if uuid in seen or uuid in disable_set:
                continue
            seen.add(uuid)
            if uuid in all_mods and uuid not in active_set:
                enable_candidates.append(uuid)

        graph_mods: dict[PackageId, AboutXmlMod] = {}
        graph_pids: list[PackageId] = []
        candidate_pids: dict[str, PackageId | None] = {}
        batch_uuid_by_pid: dict[PackageId, str] = {}
        for uuid in new_active:
            mod = all_mods.get(uuid)
            if isinstance(mod, AboutXmlMod):
                pid = PackageId(mod.package_id)
                if pid not in graph_mods:
                    graph_mods[pid] = mod
                    graph_pids.append(pid)
        for uuid in enable_candidates:
            mod = all_mods[uuid]
            if not isinstance(mod, AboutXmlMod):
                candidate_pids[uuid] = None
                continue
            pid = PackageId(mod.package_id)
            if pid not in graph_mods:
                graph_mods[pid] = mod
                graph_pids.append(pid)
            candidate_pids[uuid] = pid
            batch_uuid_by_pid.setdefault(pid, uuid)

        predecessors: dict[PackageId, set[PackageId]] = {}
        successors: dict[PackageId, set[PackageId]] = {}
        if graph_mods:
            graph = ConstraintGraph()
            graph.build(
                graph_mods,
                graph_pids,
                self._ctx.config.sort,
                self._ctx.diagnostics_service.community_rules,
            )
            for source in graph.nodes:
                for edge in graph.outgoing(source):
                    if edge.type == EdgeType.LOAD_BEFORE:
                        before, after = edge.source, edge.target
                    elif edge.type in (EdgeType.DEPENDENCY, EdgeType.LOAD_AFTER):
                        before, after = edge.target, edge.source
                    else:
                        continue
                    successors.setdefault(before, set()).add(after)
                    predecessors.setdefault(after, set()).add(before)

        batch_successors: dict[str, set[str]] = {
            uuid: set() for uuid in enable_candidates
        }
        batch_indegree = dict.fromkeys(enable_candidates, 0)
        for uuid, pid in candidate_pids.items():
            if pid is None:
                continue
            for previous_pid in predecessors.get(pid, ()):
                previous_uuid = batch_uuid_by_pid.get(previous_pid)
                if previous_uuid is None or previous_uuid == uuid:
                    continue
                if uuid not in batch_successors[previous_uuid]:
                    batch_successors[previous_uuid].add(uuid)
                    batch_indegree[uuid] += 1

        input_index = {uuid: index for index, uuid in enumerate(enable_candidates)}
        ready = [
            (input_index[uuid], uuid)
            for uuid, degree in batch_indegree.items()
            if degree == 0
        ]
        ordered_candidates: list[str] = []
        while ready:
            _, uuid = heappop(ready)
            ordered_candidates.append(uuid)
            for dependent in batch_successors[uuid]:
                batch_indegree[dependent] -= 1
                if batch_indegree[dependent] == 0:
                    heappush(ready, (input_index[dependent], dependent))

        cyclic_candidates = set(enable_candidates) - set(ordered_candidates)
        ordered_candidates.extend(
            uuid for uuid in enable_candidates if uuid in cyclic_candidates
        )

        active_positions: dict[PackageId, list[int]] = {}
        for index, uuid in enumerate(new_active):
            mod = all_mods.get(uuid)
            if isinstance(mod, AboutXmlMod):
                active_positions.setdefault(PackageId(mod.package_id), []).append(index)

        buckets: list[list[str]] = [[] for _ in range(len(new_active) + 1)]
        batch_gaps: dict[PackageId, int] = {}
        for uuid in ordered_candidates:
            pid = candidate_pids[uuid]
            if pid is None or uuid in cyclic_candidates:
                buckets[-1].append(uuid)
                if pid is not None:
                    batch_gaps[pid] = len(new_active)
                continue

            must_follow = _reachable_from(pid, predecessors)
            must_precede = _reachable_from(pid, successors)
            earliest_gap = 0
            latest_gap = len(new_active)
            has_prior_constraint = False
            has_later_constraint = False
            for previous_pid in must_follow:
                positions = active_positions.get(previous_pid)
                if positions:
                    has_prior_constraint = True
                    earliest_gap = max(earliest_gap, positions[-1] + 1)
                previous_gap = batch_gaps.get(previous_pid)
                if previous_gap is not None:
                    has_prior_constraint = True
                    earliest_gap = max(earliest_gap, previous_gap)
            for later_pid in must_precede:
                positions = active_positions.get(later_pid)
                if positions:
                    has_later_constraint = True
                    latest_gap = min(latest_gap, positions[0])

            if earliest_gap > latest_gap or not (
                has_prior_constraint or has_later_constraint
            ):
                gap = len(new_active)
            elif has_prior_constraint:
                gap = earliest_gap
            else:
                gap = latest_gap
            buckets[gap].append(uuid)
            batch_gaps[pid] = gap

        result: list[str] = []
        for index, uuid in enumerate(new_active):
            result.extend(buckets[index])
            result.append(uuid)
        result.extend(buckets[-1])

        if result == active:
            return False

        self._ctx.set_active(result)
        return True


def _reachable_from(
    pid: PackageId, adjacent: dict[PackageId, set[PackageId]]
) -> set[PackageId]:
    reached: set[PackageId] = set()
    pending = list(adjacent.get(pid, ()))
    while pending:
        current = pending.pop()
        if current in reached:
            continue
        reached.add(current)
        pending.extend(adjacent.get(current, ()))
    return reached
