from __future__ import annotations

from pxmodrim.models.metadata.structures import CaseInsensitiveStr
from pxmodrim.sort.config import Tier
from pxmodrim.sort.models import (
    DepGraphs,
    ModDiagnostics,
    SortResult,
    SortSummary,
)
from pxmodrim.sort.tiers import find_cycle

PackageId = CaseInsensitiveStr


def sort_tiers(
    graphs: DepGraphs,
    diagnostics: dict[PackageId, ModDiagnostics],
) -> SortResult:
    all_pids = set(graphs.deps.keys()) | set(graphs.rev_deps.keys())
    order: list[PackageId] = []
    cycles: list[list[PackageId]] = []

    for tier in Tier:
        tier_pids = {pid for pid, t in graphs.tiers.items() if t == tier}
        if not tier_pids:
            continue

        sub_deps = {pid: (graphs.deps.get(pid, set()) & tier_pids) for pid in tier_pids}

        indegree = {pid: len(deps) for pid, deps in sub_deps.items()}

        rev: dict[PackageId, set[PackageId]] = {}
        for pid, deps in sub_deps.items():
            for dep in deps:
                rev.setdefault(dep, set()).add(pid)

        def _tier_sort_key(pid: PackageId) -> tuple[int, str]:
            return (
                graphs.tier_priority.get(pid, len(graphs.tier_priority)),
                pid.lower(),
            )

        queue = [pid for pid, deg in indegree.items() if deg == 0]
        queue.sort(key=_tier_sort_key)

        tier_order: list[PackageId] = []
        while queue:
            pid = queue.pop(0)
            tier_order.append(pid)
            for dependent in rev.get(pid, set()):
                indegree[dependent] -= 1
                if indegree[dependent] == 0:
                    queue.append(dependent)
            queue.sort(key=_tier_sort_key)

        if len(tier_order) < len(tier_pids):
            remaining = tier_pids - set(tier_order)
            cycle = find_cycle(remaining, sub_deps)
            if cycle:
                cycles.append(cycle)
                for pid in cycle:
                    d = diagnostics[pid]
                    diagnostics[pid] = ModDiagnostics(
                        package_id=d.package_id,
                        missing_dependencies=d.missing_dependencies,
                        incompatibilities=d.incompatibilities,
                        cycle_member=True,
                        tier=d.tier,
                        warnings=d.warnings
                        + [f"Part of cycle: {' -> '.join(str(c) for c in cycle)}"],
                    )
            tier_order.extend(sorted(remaining, key=_tier_sort_key))

        order.extend(tier_order)

    summary = SortSummary(
        total_mods=len(all_pids),
        sorted_mods=len(order),
        cycle_count=len(cycles),
        missing_dep_count=sum(
            len(d.missing_dependencies) for d in diagnostics.values()
        ),
        incompat_count=sum(len(d.incompatibilities) for d in diagnostics.values()),
    )

    return SortResult(
        order=order,
        diagnostics=diagnostics,
        cycles=cycles,
        summary=summary,
        community_rules_applied=0,
    )
