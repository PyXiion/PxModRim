from __future__ import annotations

import heapq
from typing import TYPE_CHECKING

from pxmodrim.core.checker.graph import ConstraintGraph, EdgeType
from pxmodrim.core.checker.models import PackageId
from pxmodrim.core.models.metadata.structures import AboutXmlMod
from pxmodrim.core.sort.config import SortSettings, Tier, TierConfig
from pxmodrim.core.sort.models import CommunityRule
from pxmodrim.core.sort.tiers import assign_tiers

if TYPE_CHECKING:
    from ttimer import Timer


def topological_sort(
    active_mods: dict[PackageId, AboutXmlMod],
    graph: ConstraintGraph,
    settings: SortSettings,
    community_rules: dict[PackageId, CommunityRule] | None = None,
    timer: Timer | None = None,
) -> list[PackageId]:
    """Sort active mods into tiers by dependencies, load-order, and config priority."""
    from ttimer import Timer

    t = timer or Timer()
    all_pids = set(active_mods.keys())
    if not all_pids:
        return []

    with t("build_deps"):
        deps, rev_deps = _build_deps_and_rev_deps(graph, all_pids)
    with t("build_load_first"):
        load_first = _build_load_first(community_rules, all_pids, settings)
    with t("build_load_last"):
        load_last = _build_load_last(community_rules, all_pids, settings)

    with t("assign_tiers"):
        tiers = assign_tiers(
            all_pids, deps, rev_deps, load_first, load_last, settings.tier_config
        )

    order: list[PackageId] = []
    ordered: set[PackageId] = set()
    for tier in (Tier.TIER_0, Tier.TIER_1, Tier.TIER_2, Tier.TIER_3):
        tier_pids = {
            pid for pid in all_pids if pid not in ordered and tiers.get(pid) == tier
        }
        if not tier_pids:
            continue

        with t(f"kahn_{tier.name.lower()}"):
            sub_deps = {pid: deps.get(pid, set()) & tier_pids for pid in tier_pids}
            indegree = {pid: len(d) for pid, d in sub_deps.items()}

            rev: dict[PackageId, set[PackageId]] = {}
            for pid, ds in sub_deps.items():
                for d in ds:
                    rev.setdefault(d, set()).add(pid)

            config_priority = _build_config_priority(settings.tier_config)

            def _key(pid: PackageId, config_priority=config_priority) -> tuple:
                return (config_priority.get(pid, len(config_priority)), pid.lower())

            queue = [(_key(pid), pid) for pid, deg in indegree.items() if deg == 0]
            heapq.heapify(queue)

            while queue:
                _, pid = heapq.heappop(queue)
                order.append(pid)
                ordered.add(pid)
                for dependent in rev.get(pid, set()):
                    indegree[dependent] -= 1
                    if indegree[dependent] == 0:
                        heapq.heappush(queue, (_key(dependent), dependent))

            remaining = tier_pids - ordered
            if remaining:
                order.extend(sorted(remaining, key=_key))
                ordered.update(remaining)

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

        deps_pid = deps[pid]
        rev_pid = rev_deps[pid]
        for edge in graph.outgoing(pid):
            target = edge.target
            if target not in all_pids:
                continue
            if edge.type == EdgeType.LOAD_BEFORE:
                deps[target].add(pid)
                rev_pid.add(target)
            elif edge.type in (EdgeType.LOAD_AFTER, EdgeType.DEPENDENCY):
                deps_pid.add(target)
                rev_deps[target].add(pid)

    return deps, rev_deps


def _build_load_first(
    community_rules: dict[PackageId, CommunityRule] | None,
    all_pids: set[PackageId],
    settings: SortSettings,
) -> set[PackageId]:
    """Collect PIDs that should be loaded first from community rules."""
    if not settings.use_community_rules or not community_rules:
        return set()
    return {
        pid
        for pid, rule in community_rules.items()
        if pid in all_pids and rule.load_first
    }


def _build_load_last(
    community_rules: dict[PackageId, CommunityRule] | None,
    all_pids: set[PackageId],
    settings: SortSettings,
) -> set[PackageId]:
    """Collect PIDs that should be loaded last from community rules."""
    if not settings.use_community_rules or not community_rules:
        return set()
    return {
        pid
        for pid, rule in community_rules.items()
        if pid in all_pids and rule.load_last
    }


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
