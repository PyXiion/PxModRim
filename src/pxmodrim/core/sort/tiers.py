from __future__ import annotations

from pxmodrim.core.sort.config import PackageId, Tier, TierConfig


def assign_tiers(
    canonical_ids: set[PackageId],
    deps: dict[PackageId, set[PackageId]],
    rev_deps: dict[PackageId, set[PackageId]],
    load_first: set[PackageId],
    load_last: set[PackageId],
    tier_config: TierConfig,
) -> dict[PackageId, Tier]:
    """Assign each mod to a Tier (0-3) based on load order & dependency constraints."""
    tiers = dict.fromkeys(canonical_ids, Tier.TIER_2)

    tier3_set = set(load_last)
    for pid in load_last:
        tier3_set.update(get_reverse_deps_recursive(pid, rev_deps))
    for pid in tier_config.tier_3:
        if pid in canonical_ids:
            tier3_set.add(pid)
            tier3_set.update(get_reverse_deps_recursive(pid, rev_deps))
    for pid in tier3_set & canonical_ids:
        tiers[pid] = Tier.TIER_3

    tier0_set = set()
    for pid in tier_config.tier_0:
        if pid in canonical_ids:
            tier0_set.add(pid)
            tier0_set.update(get_deps_recursive(pid, deps))
    for pid in tier0_set & canonical_ids:
        tiers[pid] = Tier.TIER_0

    tier1_set = set()
    for pid in tier_config.tier_1:
        if pid in canonical_ids:
            tier1_set.add(pid)
            tier1_set.update(get_deps_recursive(pid, deps))
    for pid in load_first:
        if pid in canonical_ids:
            tier1_set.add(pid)
            tier1_set.update(get_deps_recursive(pid, deps))
    for pid in tier1_set & canonical_ids:
        if tiers[pid] == Tier.TIER_2:
            tiers[pid] = Tier.TIER_1

    return tiers


def get_deps_recursive(
    start: PackageId,
    deps: dict[PackageId, set[PackageId]],
    visited: set[PackageId] | None = None,
) -> set[PackageId]:
    """Collect all transitive dependencies for a given package."""
    if visited is None:
        visited = set()
    if start in visited:
        return set()

    visited.add(start)
    result: set[PackageId] = set()
    stack = [start]
    while stack:
        pid = stack.pop()
        for dep in deps.get(pid, set()):
            result.add(dep)
            if dep not in visited:
                visited.add(dep)
                stack.append(dep)
    return result


def get_reverse_deps_recursive(
    start: PackageId,
    rev_deps: dict[PackageId, set[PackageId]],
    visited: set[PackageId] | None = None,
) -> set[PackageId]:
    """Collect all transitive reverse-dependents for a given package."""
    if visited is None:
        visited = set()
    if start in visited:
        return set()

    visited.add(start)
    result: set[PackageId] = set()
    stack = [start]
    while stack:
        pid = stack.pop()
        for rdep in rev_deps.get(pid, set()):
            result.add(rdep)
            if rdep not in visited:
                visited.add(rdep)
                stack.append(rdep)
    return result


def find_cycle(
    nodes: set[PackageId],
    deps: dict[PackageId, set[PackageId]],
) -> list[PackageId] | None:
    """Detect a dependency cycle in the given nodes using iterative DFS."""
    visited: set[PackageId] = set()
    rec_stack: set[PackageId] = set()
    parent: dict[PackageId, PackageId] = {}

    for root in nodes:
        if root in visited:
            continue

        parent[root] = root
        visited.add(root)
        rec_stack.add(root)
        stack = [(root, iter(deps.get(root, set())))]
        while stack:
            pid, dependents = stack[-1]
            try:
                dep = next(dependents)
            except StopIteration:
                stack.pop()
                rec_stack.remove(pid)
                continue

            if dep not in nodes:
                continue
            if dep not in visited:
                parent[dep] = pid
                visited.add(dep)
                rec_stack.add(dep)
                stack.append((dep, iter(deps.get(dep, set()))))
            elif dep in rec_stack:
                cycle = [dep]
                current = pid
                while current != dep:
                    cycle.append(current)
                    current = parent[current]
                cycle.append(dep)
                return list(reversed(cycle))

    return None
