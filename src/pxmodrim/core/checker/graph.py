from __future__ import annotations

from collections.abc import Iterable, Iterator
from collections.abc import Set as AbstractSet
from enum import IntEnum, auto
from typing import TYPE_CHECKING, NamedTuple

from pxmodrim.core.models.metadata.structures import (
    AboutXmlMod,
    BaseRules,
    CaseInsensitiveStr,
)

if TYPE_CHECKING:
    from pxmodrim.core.sort.community import CommunityRule
    from pxmodrim.core.sort.config import SortSettings

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


class ConstraintEdge(NamedTuple):
    # A NamedTuple keeps hashing/equality in C; graph builds create thousands.
    source: PackageId
    target: PackageId
    type: EdgeType
    origin: EdgeOrigin

    def __repr__(self) -> str:
        return (
            f"ConstraintEdge({self.source} -> {self.target}, "
            f"type={self.type.name}, origin={self.origin.name})"
        )


# Bypasses NamedTuple's Python-level __new__ on the build hot path.
_new_edge = tuple.__new__
_EdgesByType = dict[EdgeType, set[ConstraintEdge]]
_NO_EDGES: frozenset[ConstraintEdge] = frozenset()
_NO_TYPED: dict[EdgeType, set[ConstraintEdge]] = {}
_CYCLE_TYPES = (EdgeType.DEPENDENCY, EdgeType.LOAD_AFTER)


class ConstraintGraph:
    """Directed graph modelling mod dependencies, load order, incompatibilities."""

    __slots__ = (
        "_cycle_components",
        "_incoming",
        "_nodes",
        "_ordered_pids",
        "_outgoing",
        "_pid_to_index",
    )

    def __init__(self) -> None:
        # Edges are partitioned by type so per-type queries need no filtering.
        self._outgoing: dict[PackageId, _EdgesByType] = {}
        self._incoming: dict[PackageId, _EdgesByType] = {}
        self._nodes: dict[PackageId, None] = {}
        self._pid_to_index: dict[PackageId, int] = {}
        self._ordered_pids: list[PackageId] = []
        # Cycle membership depends only on edges, so it survives reorders.
        self._cycle_components: list[list[PackageId]] | None = None

    # ── Query ─────────────────────────────────────────────────

    @property
    def nodes(self) -> AbstractSet[PackageId]:
        return self._nodes.keys()

    def has_node(self, pid: PackageId) -> bool:
        return pid in self._nodes

    def outgoing(self, pid: PackageId) -> frozenset[ConstraintEdge]:
        by_type = self._outgoing.get(pid)
        return frozenset().union(*by_type.values()) if by_type else _NO_EDGES

    def incoming(self, pid: PackageId) -> frozenset[ConstraintEdge]:
        by_type = self._incoming.get(pid)
        return frozenset().union(*by_type.values()) if by_type else _NO_EDGES

    def neighbors(self, pid: PackageId) -> frozenset[PackageId]:
        nbrs: set[PackageId] = set()
        for edges in self._outgoing.get(pid, _NO_TYPED).values():
            nbrs.update(edge.target for edge in edges)
        for edges in self._incoming.get(pid, _NO_TYPED).values():
            nbrs.update(edge.source for edge in edges)
        return frozenset(nbrs)

    def edges_of_type(
        self, pid: PackageId, edge_type: EdgeType
    ) -> AbstractSet[ConstraintEdge]:
        """Read-only view of outgoing edges of one type; callers must not mutate."""
        return self._outgoing.get(pid, _NO_TYPED).get(edge_type, _NO_EDGES)

    def incoming_of_type(
        self, pid: PackageId, edge_type: EdgeType
    ) -> AbstractSet[ConstraintEdge]:
        """Read-only view of incoming edges of one type; callers must not mutate."""
        return self._incoming.get(pid, _NO_TYPED).get(edge_type, _NO_EDGES)

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
        """Build constraint graph from active mods, order, settings, community rules."""
        self._outgoing = {}
        self._incoming = {}
        self._cycle_components = None
        self._ordered_pids = list(ordered_pids)
        self._pid_to_index = {pid: i for i, pid in enumerate(ordered_pids)}

        # Pre-populate nodes so empty mods are tracked
        self._nodes = dict.fromkeys(ordered_pids)

        alt_map = (
            _build_alt_map(active_mods) if settings.use_alternative_package_ids else {}
        )

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
        """Insert a single mod into the graph at a given index and add its edges."""
        self._cycle_components = None
        self._ensure_node(pid)
        self._ordered_pids.insert(index, pid)
        self._rebuild_index()

        alt_map = (
            _build_alt_map({pid: mod}) if settings.use_alternative_package_ids else {}
        )
        self._add_rules(pid, mod.about_rules, EdgeOrigin.ABOUT_XML, alt_map)

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
        """Remove a mod and all its incident edges from the graph."""
        self._cycle_components = None
        for edges in self._outgoing.pop(pid, _NO_TYPED).values():
            for edge in edges:
                self._incoming[edge.target][edge.type].discard(edge)
        for edges in self._incoming.pop(pid, _NO_TYPED).values():
            for edge in edges:
                self._outgoing[edge.source][edge.type].discard(edge)
        self._nodes.pop(pid, None)
        self._pid_to_index.pop(pid, None)
        self._ordered_pids = [p for p in self._ordered_pids if p != pid]
        self._rebuild_index()

    def update_order(self, ordered_pids: list[PackageId]) -> None:
        """Replace the mod ordering without rebuilding constraint edges."""
        self._ordered_pids = list(ordered_pids)
        self._pid_to_index = {pid: i for i, pid in enumerate(ordered_pids)}

    # ── Cycle detection ────────────────────────────────────────

    def find_cycles(self) -> list[list[PackageId]]:
        """Return strongly connected components that contain dependency cycles."""
        if self._cycle_components is None:
            self._cycle_components = self._find_cycle_components()
        index = self._pid_to_index
        missing = len(index)
        return [
            sorted(component, key=lambda member: index.get(member, missing))
            for component in self._cycle_components
        ]

    def _find_cycle_components(self) -> list[list[PackageId]]:
        # Nodes without dependency/load-after edges are trivial SCCs that can
        # never be part of a cycle, so they are left out of the search.
        adjacency: dict[PackageId, list[PackageId]] = {}
        for pid, by_type in self._outgoing.items():
            targets: set[PackageId] = set()
            for edge_type in _CYCLE_TYPES:
                edges = by_type.get(edge_type)
                if edges:
                    targets.update(edge.target for edge in edges)
            if targets:
                adjacency[pid] = list(targets)
        for neighbors in adjacency.values():
            neighbors[:] = [t for t in neighbors if t in adjacency]

        indices: dict[PackageId, int] = {}
        lowlinks: dict[PackageId, int] = {}
        active_stack: list[PackageId] = []
        on_stack: set[PackageId] = set()
        components: list[list[PackageId]] = []
        next_index = 0

        for root, root_neighbors in adjacency.items():
            if root in indices:
                continue

            indices[root] = next_index
            lowlinks[root] = next_index
            next_index += 1
            active_stack.append(root)
            on_stack.add(root)
            dfs_stack: list[tuple[PackageId, Iterator[PackageId]]] = [
                (root, iter(root_neighbors))
            ]

            while dfs_stack:
                pid, neighbors = dfs_stack[-1]
                neighbor = next(neighbors, None)
                if neighbor is None:
                    dfs_stack.pop()
                    if lowlinks[pid] == indices[pid]:
                        component: list[PackageId] = []
                        while True:
                            member = active_stack.pop()
                            on_stack.remove(member)
                            component.append(member)
                            if member == pid:
                                break
                        if len(component) > 1 or pid in adjacency[pid]:
                            components.append(component)
                    if dfs_stack:
                        parent = dfs_stack[-1][0]
                        lowlinks[parent] = min(lowlinks[parent], lowlinks[pid])
                    continue

                if neighbor not in indices:
                    indices[neighbor] = next_index
                    lowlinks[neighbor] = next_index
                    next_index += 1
                    active_stack.append(neighbor)
                    on_stack.add(neighbor)
                    dfs_stack.append((neighbor, iter(adjacency[neighbor])))
                elif neighbor in on_stack and indices[neighbor] < lowlinks[pid]:
                    lowlinks[pid] = indices[neighbor]

        return components

    # ── Private helpers ────────────────────────────────────────

    def _ensure_node(self, pid: PackageId) -> None:
        if pid not in self._nodes:
            self._nodes[pid] = None

    def _add_edges(
        self,
        source: PackageId,
        targets: Iterable[PackageId],
        type: EdgeType,
        origin: EdgeOrigin,
        alt_map: dict[PackageId, PackageId] | None,
    ) -> None:
        # Hot path of build(): one call per rule set rather than per edge.
        nodes = self._nodes
        incoming = self._incoming
        out_edges: set[ConstraintEdge] | None = None
        for target in targets:
            if alt_map:
                target = alt_map.get(target, target)
            if out_edges is None:
                if source not in nodes:
                    nodes[source] = None
                by_type = self._outgoing.get(source)
                if by_type is None:
                    by_type = self._outgoing[source] = {}
                out_edges = by_type.get(type)
                if out_edges is None:
                    out_edges = by_type[type] = set()
            if target not in nodes:
                nodes[target] = None
            edge = _new_edge(ConstraintEdge, (source, target, type, origin))
            out_edges.add(edge)
            in_by_type = incoming.get(target)
            if in_by_type is None:
                incoming[target] = {type: {edge}}
            elif (in_edges := in_by_type.get(type)) is None:
                in_by_type[type] = {edge}
            else:
                in_edges.add(edge)

    def _add_rules(
        self,
        pid: PackageId,
        rules: BaseRules,
        origin: EdgeOrigin,
        alt_map: dict[PackageId, PackageId],
    ) -> None:
        for dep_id, dep_mod in rules.dependencies.items():
            self._add_edges(pid, (dep_id,), EdgeType.DEPENDENCY, origin, alt_map)
            if dep_mod.alternative_package_ids:
                self._add_edges(
                    pid,
                    dep_mod.alternative_package_ids,
                    EdgeType.ALTERNATIVE,
                    origin,
                    None,
                )

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
        if load_after:
            self._add_edges(pid, load_after, EdgeType.LOAD_AFTER, origin, alt_map)
        if load_before:
            self._add_edges(pid, load_before, EdgeType.LOAD_BEFORE, origin, alt_map)
        if incompatible_with:
            self._add_edges(
                pid, incompatible_with, EdgeType.INCOMPATIBILITY, origin, alt_map
            )

    def _rebuild_index(self) -> None:
        self._pid_to_index = {pid: i for i, pid in enumerate(self._ordered_pids)}


def _build_alt_map(
    active_mods: dict[PackageId, AboutXmlMod],
) -> dict[PackageId, PackageId]:
    """Map canonical package IDs to active alternatives that satisfy them."""
    alt_map: dict[PackageId, PackageId] = {}
    for mod in active_mods.values():
        for dep_id, dep in mod.about_rules.dependencies.items():
            if dep_id in active_mods:
                continue
            for alternative_pid in dep.alternative_package_ids:
                if alternative_pid in active_mods:
                    alt_map[dep_id] = alternative_pid
                    break
    return alt_map
