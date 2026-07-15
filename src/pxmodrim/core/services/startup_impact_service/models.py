from __future__ import annotations

from dataclasses import dataclass

IMPACT_WARN_THRESHOLD_S = 1.0
IMPACT_HIGH_THRESHOLD_S = 5.0


@dataclass(frozen=True, slots=True)
class StartupImpactMod:
    mod_name: str
    package_id: str | None
    total_impact_s: float


@dataclass(frozen=True, slots=True)
class StartupImpactReport:
    path: str
    loading_time_s: float
    mods: tuple[StartupImpactMod, ...]

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
