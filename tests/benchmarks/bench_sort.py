from __future__ import annotations

import random
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

from loguru import logger

logger.remove()

from pxmodrim.core.checker.graph import ConstraintGraph  # noqa: E402
from pxmodrim.core.checker.sort import topological_sort  # noqa: E402
from pxmodrim.core.models.metadata.structures import (  # noqa: E402
    AboutXmlMod,
    BaseRules,
    CaseInsensitiveSet,
    CaseInsensitiveStr,
    DependencyMod,
)
from pxmodrim.core.sort.config import SortSettings, TierConfig  # noqa: E402
from pxmodrim.core.sort.models import CommunityRule  # noqa: E402

PackageId = CaseInsensitiveStr


def _tier_config(pids: list[PackageId]) -> TierConfig:
    return TierConfig(
        tier_0=tuple(pids[:3]) if len(pids) >= 3 else tuple(pids),
        tier_1=tuple(pids[3:6]) if len(pids) >= 6 else (),
        tier_3=(),
    )


def make_mods(
    count: int, edges_per_mod: int
) -> tuple[
    dict[PackageId, AboutXmlMod],
    dict[PackageId, CommunityRule],
]:
    rng = random.Random(42)
    pids = [PackageId(f"bench.mod.{i:04d}") for i in range(count)]
    community_rules: dict[PackageId, CommunityRule] = {}
    mods: dict[PackageId, AboutXmlMod] = {}

    for i, pid in enumerate(pids):
        deps: dict[PackageId, DependencyMod] = {}
        load_after = CaseInsensitiveSet()

        num_edges = min(edges_per_mod, i)
        if num_edges:
            targets = rng.sample(range(i), k=num_edges)
            dep_count = max(1, num_edges // 2)
            for idx in targets[:dep_count]:
                deps[pids[idx]] = DependencyMod(
                    name=str(pids[idx]),
                    package_id=pids[idx],
                )
            for idx in targets[dep_count:]:
                load_after.add(pids[idx])

        mods[pid] = AboutXmlMod(
            name=f"Bench Mod {i:04d}",
            package_id=pid,
            provider_id="bench",
            valid=True,
            about_rules=BaseRules(dependencies=deps, load_after=load_after),
        )

        if rng.random() < 0.2:
            cr_la: set[PackageId] = set()
            if i > 0 and rng.random() < 0.5:
                extra = rng.randint(1, min(2, i))
                for idx in rng.sample(range(i), k=extra):
                    cr_la.add(pids[idx])

            community_rules[pid] = CommunityRule(
                package_id=pid,
                load_after=cr_la,
                load_before=set(),
                load_first=rng.random() < 0.1,
                load_last=rng.random() < 0.1,
                incompatible_with=set(),
            )

    return mods, community_rules


def bench(count: int, edges_per_mod: int) -> None:
    mods, community_rules = make_mods(count, edges_per_mod)
    pids = list(mods.keys())
    settings = SortSettings(
        use_community_rules=True,
        use_alternative_package_ids=False,
        tier_config=_tier_config(pids),
    )

    graph = ConstraintGraph()

    t0 = time.perf_counter_ns()
    graph.build(mods, pids, settings, community_rules)
    t1 = time.perf_counter_ns()

    t2 = time.perf_counter_ns()
    result = topological_sort(mods, graph, settings, community_rules)
    t3 = time.perf_counter_ns()

    build_ms = (t1 - t0) / 1e6
    sort_ms = (t3 - t2) / 1e6
    total_ms = (t3 - t0) / 1e6

    assert len(result) == count

    print(
        f"{count:>5} mods ({edges_per_mod} e/m): "
        f"graph={build_ms:>6.1f}ms  "
        f"sort={sort_ms:>6.1f}ms  "
        f"total={total_ms:>6.1f}ms"
    )
    print(
        f"  avg per mod: "
        f"graph={build_ms/count*1000:>5.1f}us  "
        f"sort={sort_ms/count*1000:>5.1f}us  "
        f"total={total_ms/count*1000:>5.1f}us"
    )


if __name__ == "__main__":
    print("=== bench_sort ===")
    for count in (200, 500, 1000, 2000):
        bench(count, edges_per_mod=5)
    bench(2000, edges_per_mod=10)
