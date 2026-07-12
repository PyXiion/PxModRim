from __future__ import annotations

from loguru import logger

from pxmodrim.models.metadata.structures import (
    AboutXmlMod,
    BaseRules,
)
from pxmodrim.sort.community import CommunityRule, merge_community_rules
from pxmodrim.sort.config import SortSettings
from pxmodrim.sort.models import (
    DepGraphs,
    ModDiagnostics,
    PackageId,
)
from pxmodrim.sort.tiers import assign_tiers


def build_graphs(
    active_mods: dict[str, AboutXmlMod],
    settings: SortSettings,
    community_rules: dict[PackageId, CommunityRule] | None = None,
) -> tuple[DepGraphs, dict[PackageId, ModDiagnostics]]:
    canonical_ids: set[PackageId] = set()
    alt_map: dict[PackageId, PackageId] = {}
    mod_by_pid: dict[PackageId, AboutXmlMod] = {}
    base_rules_by_pid: dict[PackageId, BaseRules] = {}

    for mod in active_mods.values():
        pid = mod.package_id
        canonical_ids.add(pid)
        mod_by_pid[pid] = mod
        base_rules_by_pid[pid] = mod.about_rules

        for dep in mod.about_rules.dependencies.values():
            for alt in dep.alternative_package_ids:
                alt_map[alt] = pid

    if settings.use_community_rules and community_rules:
        for pid, rules in base_rules_by_pid.items():
            base_rules_by_pid[pid] = merge_community_rules(rules, community_rules, pid)

    raw_deps: dict[PackageId, set[PackageId]] = {}
    raw_rev_deps: dict[PackageId, set[PackageId]] = {}
    raw_incompat: dict[PackageId, set[PackageId]] = {}
    load_first: set[PackageId] = set()
    load_last: set[PackageId] = set()

    for pid, rules in base_rules_by_pid.items():
        raw_deps[pid] = set(rules.load_after)

        for before in rules.load_before:
            raw_rev_deps.setdefault(before, set()).add(pid)

        force_after: set[PackageId] = getattr(rules, "force_load_after", set())
        raw_deps[pid].update(force_after)
        force_before: set[PackageId] = getattr(rules, "force_load_before", set())
        for before in force_before:
            raw_rev_deps.setdefault(before, set()).add(pid)

        if settings.use_moddependencies_as_load_before:
            for dep in rules.dependencies.values():
                raw_deps[pid].add(dep.package_id)

        raw_incompat[pid] = set(rules.incompatible_with)

        if getattr(rules, "load_first", False):
            load_first.add(pid)
        if getattr(rules, "load_last", False):
            load_last.add(pid)

    if settings.use_community_rules and community_rules:
        for pid, cr in community_rules.items():
            if pid in canonical_ids:
                if cr.load_first:
                    load_first.add(pid)
                if cr.load_last:
                    load_last.add(pid)

    def resolve(pid: PackageId) -> PackageId:
        return alt_map.get(pid, pid) if settings.use_alternative_package_ids else pid

    deps = {resolve(k): {resolve(d) for d in v} for k, v in raw_deps.items()}
    rev_deps = {resolve(k): {resolve(d) for d in v} for k, v in raw_rev_deps.items()}
    incompat = {resolve(k): {resolve(d) for d in v} for k, v in raw_incompat.items()}
    load_first = {resolve(p) for p in load_first}
    load_last = {resolve(p) for p in load_last}

    canonical_resolved = {resolve(p) for p in canonical_ids}
    for graph in (deps, rev_deps, incompat):
        for k in list(graph.keys()):
            if k not in canonical_resolved:
                del graph[k]
            else:
                graph[k] = {d for d in graph[k] if d in canonical_resolved}

    diagnostics: dict[PackageId, ModDiagnostics] = {}
    for pid in canonical_resolved:
        missing = [d for d in deps.get(pid, set()) if d not in canonical_resolved]
        incompat_list = [i for i in incompat.get(pid, set()) if i in canonical_resolved]

        diagnostics[pid] = ModDiagnostics(
            package_id=pid,
            missing_dependencies=missing,
            incompatibilities=incompat_list,
        )

    tiers = assign_tiers(
        canonical_resolved, deps, rev_deps, load_first, load_last, settings.tier_config
    )
    for pid, tier in tiers.items():
        d = diagnostics[pid]
        diagnostics[pid] = ModDiagnostics(
            package_id=d.package_id,
            missing_dependencies=d.missing_dependencies,
            incompatibilities=d.incompatibilities,
            tier=tier,
        )

    tier_priority: dict[PackageId, int] = {}
    tc = settings.tier_config
    for i, pid in enumerate(tc.tier_0):
        tier_priority[pid] = i
    for i, pid in enumerate(tc.tier_1):
        if pid not in tier_priority:
            tier_priority[pid] = len(tc.tier_0) + i
    for i, pid in enumerate(tc.tier_3):
        if pid not in tier_priority:
            tier_priority[pid] = len(tc.tier_0) + len(tc.tier_1) + i

    logger.debug(
        f"Built graphs: {len(deps)} mods, {len(canonical_resolved)} canonical IDs"
    )
    return DepGraphs(
        deps=deps,
        rev_deps=rev_deps,
        incompat=incompat,
        tiers=tiers,
        alt_map=alt_map,
        load_first=load_first,
        load_last=load_last,
        tier_priority=tier_priority,
    ), diagnostics
