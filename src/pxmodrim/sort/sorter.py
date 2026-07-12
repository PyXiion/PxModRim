from __future__ import annotations

from pxmodrim.models.metadata.structures import AboutXmlMod, ListedMod
from pxmodrim.sort.community import load_community_rules
from pxmodrim.sort.config import SortSettings
from pxmodrim.sort.graph import build_graphs
from pxmodrim.sort.models import SortResult
from pxmodrim.sort.topological import sort_tiers


def sort_active_mods(
    mods: dict[str, ListedMod],
    active_uuids: list[str],
    settings: SortSettings,
    community_rules_path: str | None = None,
) -> SortResult:
    active_set = set(active_uuids)
    active_mods = {
        uuid: mod
        for uuid, mod in mods.items()
        if uuid in active_set and isinstance(mod, AboutXmlMod)
    }

    community_rules = None
    if settings.use_community_rules and community_rules_path:
        from pathlib import Path

        community_rules = load_community_rules(Path(community_rules_path))

    graphs, diagnostics = build_graphs(active_mods, settings, community_rules)
    result = sort_tiers(graphs, diagnostics)

    if community_rules:
        applied = sum(
            1
            for pid in result.diagnostics
            if pid in community_rules
            and (
                community_rules[pid].load_after
                or community_rules[pid].load_before
                or community_rules[pid].load_first
                or community_rules[pid].load_last
            )
        )
        result = SortResult(
            order=result.order,
            diagnostics=result.diagnostics,
            cycles=result.cycles,
            summary=result.summary,
            community_rules_applied=applied,
        )

    return result
