from __future__ import annotations

from pxmodrim.checker.graph import ConstraintGraph, EdgeType
from pxmodrim.checker.models import PackageId
from pxmodrim.models.metadata.structures import AboutXmlMod
from pxmodrim.sort.config import SortSettings, Tier, TierConfig
from pxmodrim.sort.models import CommunityRule
from pxmodrim.sort.tiers import assign_tiers


def topological_sort(
    active_mods: dict[PackageId, AboutXmlMod],
    graph: ConstraintGraph,
    settings: SortSettings,
    community_rules: dict[PackageId, CommunityRule] | None = None,
) -> list[PackageId]:
    """Sort active mods into tiers by dependencies, load-order, and config priority."""
    all_pids = set(active_mods.keys())
    if not all_pids:
        return []

    deps, rev_deps = _build_deps_and_rev_deps(graph, all_pids)
    load_first = _build_load_first(active_mods, community_rules, all_pids, settings)
    load_last = _build_load_last(active_mods, community_rules, all_pids, settings)

    tiers = assign_tiers(
        all_pids, deps, rev_deps, load_first, load_last, settings.tier_config
    )

    order: list[PackageId] = []
    for tier in (Tier.TIER_0, Tier.TIER_1, Tier.TIER_2, Tier.TIER_3):
        tier_pids = {
            pid for pid in all_pids if pid not in order and tiers.get(pid) == tier
        }
        if not tier_pids:
            continue

        sub_deps = {pid: deps.get(pid, set()) & tier_pids for pid in tier_pids}
        indegree = {pid: len(d) for pid, d in sub_deps.items()}

        rev: dict[PackageId, set[PackageId]] = {}
        for pid, ds in sub_deps.items():
            for d in ds:
                rev.setdefault(d, set()).add(pid)

        config_priority = _build_config_priority(settings.tier_config)

        def _key(pid: PackageId, config_priority=config_priority) -> tuple:
            return (config_priority.get(pid, len(config_priority)), pid.lower())

        queue = [pid for pid, deg in indegree.items() if deg == 0]
        queue.sort(key=_key)

        while queue:
            pid = queue.pop(0)
            order.append(pid)
            for dependent in rev.get(pid, set()):
                indegree[dependent] -= 1
                if indegree[dependent] == 0:
                    queue.append(dependent)
            queue.sort(key=_key)

        remaining = tier_pids - set(order)
        if remaining:
            order.extend(sorted(remaining, key=_key))

    return order


def _build_deps_and_rev_deps(
    graph: ConstraintGraph,
    all_pids: set[PackageId],
) -> tuple[dict[PackageId, set[PackageId]], dict[PackageId, set[PackageId]]]:
    """Extract dependency and reverse-dependency maps from constraint graph edges."""
    deps: dict[PackageId, set[PackageId]] = {pid: set() for pid in all_pids}
    rev_deps: dict[PackageId, set[PackageId]] = {pid: set() for pid in all_pids}

    for pid in graph.nodes:
        if pid not in all_pids:
            continue

        for edge in graph.edges_of_type(pid, EdgeType.LOAD_BEFORE):
            if edge.target in all_pids:
                deps.setdefault(edge.target, set()).add(pid)
                rev_deps.setdefault(pid, set()).add(edge.target)

        for edge in graph.edges_of_type(pid, EdgeType.LOAD_AFTER):
            if edge.target in all_pids:
                deps.setdefault(pid, set()).add(edge.target)
                rev_deps.setdefault(edge.target, set()).add(pid)

        for edge in graph.edges_of_type(pid, EdgeType.DEPENDENCY):
            if edge.target in all_pids:
                deps.setdefault(pid, set()).add(edge.target)
                rev_deps.setdefault(edge.target, set()).add(pid)

    return deps, rev_deps


def _build_load_first(
    active_mods: dict[PackageId, AboutXmlMod],
    community_rules: dict[PackageId, CommunityRule] | None,
    all_pids: set[PackageId],
    settings: SortSettings,
) -> set[PackageId]:
    """Collect PIDs that should be loaded first from mod rules and community rules."""
    load_first: set[PackageId] = set()
    for pid in all_pids:
        mod = active_mods.get(pid)
        if mod and mod.overall_rules.load_first:
            load_first.add(pid)
    if settings.use_community_rules and community_rules:
        for pid, cr in community_rules.items():
            if pid in all_pids and cr.load_first:
                load_first.add(pid)
    return load_first


def _build_load_last(
    active_mods: dict[PackageId, AboutXmlMod],
    community_rules: dict[PackageId, CommunityRule] | None,
    all_pids: set[PackageId],
    settings: SortSettings,
) -> set[PackageId]:
    """Collect PIDs that should be loaded last from mod rules and community rules."""
    load_last: set[PackageId] = set()
    for pid in all_pids:
        mod = active_mods.get(pid)
        if mod and mod.overall_rules.load_last:
            load_last.add(pid)
    if not settings.use_community_rules or not community_rules:
        return load_last
    for pid, cr in community_rules.items():
        if pid in all_pids and cr.load_last:
            load_last.add(pid)
    return load_last


def _build_config_priority(tier_config: TierConfig) -> dict[PackageId, int]:
    """Build a stable ordering priority map from tier-config package lists."""
    priority: dict[PackageId, int] = {}
    for i, pid in enumerate(tier_config.tier_0):
        priority[pid] = i
    for i, pid in enumerate(tier_config.tier_1):
        if pid not in priority:
            priority[pid] = len(tier_config.tier_0) + i
    for i, pid in enumerate(tier_config.tier_3):
        if pid not in priority:
            priority[pid] = len(tier_config.tier_0) + len(tier_config.tier_1) + i
    return priority
