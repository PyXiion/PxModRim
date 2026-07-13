from __future__ import annotations

from collections.abc import Set as AbstractSet
from enum import IntEnum, auto
from typing import TYPE_CHECKING

from pxmodrim.models.metadata.structures import (
    AboutXmlMod,
    BaseRules,
    CaseInsensitiveStr,
)
from pxmodrim.sort.tiers import find_cycle

if TYPE_CHECKING:
    from pxmodrim.sort.community import CommunityRule
    from pxmodrim.sort.config import SortSettings

PackageId = CaseInsensitiveStr


class EdgeType(IntEnum):
    DEPENDENCY = auto()
    LOAD_AFTER = auto()
    LOAD_BEFORE = auto()
    INCOMPATIBILITY = auto()
    ALTERNATIVE = auto()


class EdgeOrigin(IntEnum):
    ABOUT_XML = auto()
    COMMUNITY_RULES = auto()


class ConstraintEdge:
    __slots__ = ("source", "target", "type", "origin")

    def __init__(
        self,
        source: PackageId,
        target: PackageId,
        type: EdgeType,
        origin: EdgeOrigin,
    ) -> None:
        self.source = source
        self.target = target
        self.type = type
        self.origin = origin

    def __hash__(self) -> int:
        return hash((self.source, self.target, self.type, self.origin))

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, ConstraintEdge):
            return NotImplemented
        return (
            self.source == other.source
            and self.target == other.target
            and self.type == other.type
            and self.origin == other.origin
        )

    def __repr__(self) -> str:
        return (
            f"ConstraintEdge({self.source} -> {self.target}, "
            f"type={self.type.name}, origin={self.origin.name})"
        )


class ConstraintGraph:
    def __init__(self) -> None:
        self._outgoing: dict[PackageId, set[ConstraintEdge]] = {}
        self._incoming: dict[PackageId, set[ConstraintEdge]] = {}
        self._pid_to_index: dict[PackageId, int] = {}
        self._ordered_pids: list[PackageId] = []

    # ── Query ─────────────────────────────────────────────────

    @property
    def nodes(self) -> AbstractSet[PackageId]:
        return self._outgoing.keys()

    def has_node(self, pid: PackageId) -> bool:
        return pid in self._outgoing

    def outgoing(self, pid: PackageId) -> frozenset[ConstraintEdge]:
        return frozenset(self._outgoing.get(pid, set()))

    def incoming(self, pid: PackageId) -> frozenset[ConstraintEdge]:
        return frozenset(self._incoming.get(pid, set()))

    def neighbors(self, pid: PackageId) -> frozenset[PackageId]:
        nbrs: set[PackageId] = set()
        for edge in self._outgoing.get(pid, set()):
            nbrs.add(edge.target)
        for edge in self._incoming.get(pid, set()):
            nbrs.add(edge.source)
        return frozenset(nbrs)

    def edges_of_type(
        self, pid: PackageId, edge_type: EdgeType
    ) -> frozenset[ConstraintEdge]:
        return frozenset(
            e for e in self._outgoing.get(pid, set()) if e.type == edge_type
        )

    def incoming_of_type(
        self, pid: PackageId, edge_type: EdgeType
    ) -> frozenset[ConstraintEdge]:
        return frozenset(
            e for e in self._incoming.get(pid, set()) if e.type == edge_type
        )

    def index_of(self, pid: PackageId) -> int:
        return self._pid_to_index.get(pid, -1)

    @property
    def pid_to_index(self) -> dict[PackageId, int]:
        return dict(self._pid_to_index)

    def ordered_pids(self) -> list[PackageId]:
        return list(self._ordered_pids)

    # ── Build ──────────────────────────────────────────────────

    def build(
        self,
        active_mods: dict[PackageId, AboutXmlMod],
        ordered_pids: list[PackageId],
        settings: SortSettings,
        community_rules: dict[PackageId, CommunityRule] | None = None,
    ) -> None:
        self._outgoing.clear()
        self._incoming.clear()
        self._ordered_pids = list(ordered_pids)
        self._pid_to_index = {pid: i for i, pid in enumerate(ordered_pids)}

        # Pre-populate nodes so empty mods are tracked
        for pid in ordered_pids:
            self._ensure_node(pid)

        alt_map = _build_alt_map(active_mods)

        for pid, mod in active_mods.items():
            self._add_rules(pid, mod.about_rules, EdgeOrigin.ABOUT_XML, alt_map)

        if settings.use_community_rules and community_rules:
            for pid in active_mods:
                if pid in community_rules:
                    cr = community_rules[pid]
                    self._add_load_rules(
                        pid,
                        cr.load_before,
                        cr.load_after,
                        cr.incompatible_with,
                        EdgeOrigin.COMMUNITY_RULES,
                        alt_map,
                    )

    # ── Incremental ops ────────────────────────────────────────

    def add_mod(
        self,
        pid: PackageId,
        mod: AboutXmlMod,
        index: int,
        settings: SortSettings,
        community_rules: dict[PackageId, CommunityRule] | None = None,
    ) -> None:
        self._ensure_node(pid)
        self._ordered_pids.insert(index, pid)
        self._rebuild_index()

        self._add_rules(
            pid, mod.about_rules, EdgeOrigin.ABOUT_XML, _build_alt_map({pid: mod})
        )

        if settings.use_community_rules and community_rules and pid in community_rules:
            cr = community_rules[pid]
            self._add_load_rules(
                pid,
                cr.load_before,
                cr.load_after,
                cr.incompatible_with,
                EdgeOrigin.COMMUNITY_RULES,
                _build_alt_map({pid: mod}),
            )

    def remove_mod(self, pid: PackageId) -> None:
        self._remove_outgoing(pid)
        self._remove_incoming(pid)
        self._pid_to_index.pop(pid, None)
        self._ordered_pids = [p for p in self._ordered_pids if p != pid]
        self._rebuild_index()

    def update_order(self, ordered_pids: list[PackageId]) -> None:
        self._ordered_pids = list(ordered_pids)
        self._pid_to_index = {pid: i for i, pid in enumerate(ordered_pids)}

    # ── Cycle detection ────────────────────────────────────────

    def find_cycles(self) -> list[list[PackageId]]:
        dep_graph: dict[PackageId, set[PackageId]] = {}
        for pid in self._outgoing:
            deps: set[PackageId] = set()
            for edge in self._outgoing[pid]:
                if edge.type in (EdgeType.DEPENDENCY, EdgeType.LOAD_AFTER):
                    deps.add(edge.target)
            if deps:
                dep_graph[pid] = deps

        all_nodes = set(dep_graph.keys())
        found_cycles: list[list[PackageId]] = []

        remaining = set(all_nodes)
        while remaining:
            cycle = find_cycle(remaining, dep_graph)
            if cycle:
                cycle_set = set(cycle)
                remaining -= cycle_set
                found_cycles.append(cycle)
            else:
                break

        return found_cycles

    # ── Private helpers ────────────────────────────────────────

    def _ensure_node(self, pid: PackageId) -> None:
        self._outgoing.setdefault(pid, set())
        self._incoming.setdefault(pid, set())

    def _add_edge(
        self,
        source: PackageId,
        target: PackageId,
        type: EdgeType,
        origin: EdgeOrigin,
    ) -> None:
        self._ensure_node(source)
        self._ensure_node(target)
        edge = ConstraintEdge(source=source, target=target, type=type, origin=origin)
        self._outgoing[source].add(edge)
        self._incoming[target].add(edge)

    def _remove_outgoing(self, pid: PackageId) -> None:
        for edge in list(self._outgoing.get(pid, set())):
            self._incoming[edge.target].discard(edge)
        self._outgoing.pop(pid, None)

    def _remove_incoming(self, pid: PackageId) -> None:
        for edge in list(self._incoming.get(pid, set())):
            self._outgoing[edge.source].discard(edge)
        self._incoming.pop(pid, None)

    def _add_rules(
        self,
        pid: PackageId,
        rules: BaseRules,
        origin: EdgeOrigin,
        alt_map: dict[PackageId, PackageId],
    ) -> None:
        for dep_id, dep_mod in rules.dependencies.items():
            pid_resolved = alt_map.get(dep_id, dep_id)
            self._add_edge(pid, pid_resolved, EdgeType.DEPENDENCY, origin)
            for alt in dep_mod.alternative_package_ids:
                self._add_edge(pid, alt, EdgeType.ALTERNATIVE, origin)

        self._add_load_rules(
            pid,
            rules.load_before,
            rules.load_after,
            rules.incompatible_with,
            origin,
            alt_map,
        )

    def _add_load_rules(
        self,
        pid: PackageId,
        load_before: AbstractSet[PackageId],
        load_after: AbstractSet[PackageId],
        incompatible_with: AbstractSet[PackageId],
        origin: EdgeOrigin,
        alt_map: dict[PackageId, PackageId],
    ) -> None:
        for target in load_after:
            resolved = alt_map.get(target, target)
            self._add_edge(pid, resolved, EdgeType.LOAD_AFTER, origin)

        for target in load_before:
            resolved = alt_map.get(target, target)
            self._add_edge(pid, resolved, EdgeType.LOAD_BEFORE, origin)

        for target in incompatible_with:
            resolved = alt_map.get(target, target)
            self._add_edge(pid, resolved, EdgeType.INCOMPATIBILITY, origin)

    def _rebuild_index(self) -> None:
        self._pid_to_index = {pid: i for i, pid in enumerate(self._ordered_pids)}


def _build_alt_map(
    active_mods: dict[PackageId, AboutXmlMod],
) -> dict[PackageId, PackageId]:
    alt_map: dict[PackageId, PackageId] = {}
    for pid, mod in active_mods.items():
        for dep in mod.about_rules.dependencies.values():
            for alt in dep.alternative_package_ids:
                alt_map[alt] = pid
    return alt_map
