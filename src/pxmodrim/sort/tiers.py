from __future__ import annotations

from pxmodrim.sort.config import PackageId, Tier, TierConfig


def assign_tiers(
    canonical_ids: set[PackageId],
    deps: dict[PackageId, set[PackageId]],
    rev_deps: dict[PackageId, set[PackageId]],
    load_first: set[PackageId],
    load_last: set[PackageId],
    tier_config: TierConfig,
) -> dict[PackageId, Tier]:
    tiers = {pid: Tier.TIER_2 for pid in canonical_ids}

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
    if visited is None:
        visited = set()
    if start in visited:
        return set()
    visited.add(start)
    result = set()
    for dep in deps.get(start, set()):
        result.add(dep)
        result.update(get_deps_recursive(dep, deps, visited))
    return result


def get_reverse_deps_recursive(
    start: PackageId,
    rev_deps: dict[PackageId, set[PackageId]],
    visited: set[PackageId] | None = None,
) -> set[PackageId]:
    if visited is None:
        visited = set()
    if start in visited:
        return set()
    visited.add(start)
    result = set()
    for rdep in rev_deps.get(start, set()):
        result.add(rdep)
        result.update(get_reverse_deps_recursive(rdep, rev_deps, visited))
    return result


def find_cycle(
    nodes: set[PackageId],
    deps: dict[PackageId, set[PackageId]],
) -> list[PackageId] | None:
    visited = set()
    rec_stack = set()
    parent: dict[PackageId, PackageId] = {}

    def dfs(v: PackageId) -> list[PackageId] | None:
        visited.add(v)
        rec_stack.add(v)
        for w in deps.get(v, set()):
            if w not in nodes:
                continue
            if w not in visited:
                parent[w] = v
                cycle = dfs(w)
                if cycle:
                    return cycle
            elif w in rec_stack:
                cycle = [w]
                cur = v
                while cur != w:
                    cycle.append(cur)
                    cur = parent[cur]
                cycle.append(w)
                return list(reversed(cycle))
        rec_stack.remove(v)
        return None

    for node in nodes:
        if node not in visited:
            parent[node] = node
            cycle = dfs(node)
            if cycle:
                return cycle
    return None
