from __future__ import annotations

from dataclasses import dataclass, field

from pxmodrim.models.metadata.structures import CaseInsensitiveStr
from pxmodrim.sort.config import Tier

PackageId = CaseInsensitiveStr


@dataclass(frozen=True, slots=True)
class ModDiagnostics:
    package_id: PackageId
    missing_dependencies: list[PackageId] = field(default_factory=list)
    incompatibilities: list[PackageId] = field(default_factory=list)
    cycle_member: bool = False
    tier: Tier = Tier.TIER_2
    warnings: list[str] = field(default_factory=list)


@dataclass(frozen=True, slots=True)
class SortResult:
    order: list[PackageId]
    diagnostics: dict[PackageId, ModDiagnostics]
    cycles: list[list[PackageId]]
    summary: "SortSummary"
    community_rules_applied: int = 0

    @property
    def has_errors(self) -> bool:
        return any(d.cycle_member for d in self.diagnostics.values())

    @property
    def has_warnings(self) -> bool:
        return any(
            d.missing_dependencies or d.incompatibilities
            for d in self.diagnostics.values()
        )


@dataclass(frozen=True, slots=True)
class SortSummary:
    total_mods: int
    sorted_mods: int
    cycle_count: int
    missing_dep_count: int
    incompat_count: int


@dataclass(frozen=True, slots=True)
class DepGraphs:
    deps: dict[PackageId, set[PackageId]]
    rev_deps: dict[PackageId, set[PackageId]]
    incompat: dict[PackageId, set[PackageId]]
    tiers: dict[PackageId, Tier]
    alt_map: dict[PackageId, PackageId]
    load_first: set[PackageId]
    load_last: set[PackageId]
    tier_priority: dict[PackageId, int]


@dataclass(frozen=True, slots=True)
class CommunityRule:
    package_id: PackageId
    load_after: set[PackageId]
    load_before: set[PackageId]
    load_first: bool
    load_last: bool
    incompatible_with: set[PackageId]
