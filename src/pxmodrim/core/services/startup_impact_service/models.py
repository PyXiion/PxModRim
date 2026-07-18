from __future__ import annotations

from dataclasses import dataclass, field

IMPACT_WARN_THRESHOLD_S = 1.0
IMPACT_HIGH_THRESHOLD_S = 5.0


@dataclass(frozen=True, slots=True)
class StartupImpactMod:
    mod_name: str
    package_id: str | None
    total_impact_s: float
    metrics: dict[str, float] = field(default_factory=dict)
    off_thread_metrics: dict[str, float] = field(default_factory=dict)
    off_thread_total_impact_s: float = 0.0


@dataclass(frozen=True, slots=True)
class StartupImpactReport:
    path: str
    loading_time_s: float
    mods: tuple[StartupImpactMod, ...]
    timestamp: str = ""
    metrics: dict[str, float] = field(default_factory=dict)
    total_impact: float = 0.0
    off_thread_metrics: dict[str, float] = field(default_factory=dict)
    off_thread_total_impact: float = 0.0

    @property
    def total_time(self) -> float:
        return sum(m.total_impact_s for m in self.mods)

    @property
    def off_thread_total_time(self) -> float:
        return sum(m.off_thread_total_impact_s for m in self.mods)

    @property
    def mods_total_time(self) -> float:
        return self.total_time + self.off_thread_total_time

    @property
    def base_game_time(self) -> float:
        return sum(self.metrics.values())

    def find(
        self, package_id: str | None, mod_name: str | None
    ) -> StartupImpactMod | None:
        if package_id:
            normalized = normalize_package_id(package_id)
            for mod in self.mods:
                if mod.package_id == normalized:
                    return mod
        if mod_name:
            name_lower = mod_name.lower()
            for mod in self.mods:
                if mod.mod_name.lower() == name_lower:
                    return mod
        return None


def normalize_package_id(package_id: str) -> str:
    normalized = package_id.lower()
    if normalized.endswith("_steam"):
        normalized = normalized[: -len("_steam")]
    return normalized


def format_impact(seconds: float) -> str:
    return f"{round(seconds * 1000)}ms"
