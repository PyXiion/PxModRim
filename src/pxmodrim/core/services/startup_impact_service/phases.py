from __future__ import annotations

from enum import StrEnum

from pxmodrim.core.services.startup_impact_service.labels import strip_metric_prefix


class Phase(StrEnum):
    CONTENT = "content"
    MOD_CONSTRUCTORS = "mod_constructors"
    STATIC_CONSTRUCTORS = "static_constructors"
    PATCHES = "patches"
    DEFS = "defs"
    DEFERRED = "deferred"
    OTHER = "other"


PHASE_LABELS: dict[Phase, str] = {
    Phase.CONTENT: "Textures & audio",
    Phase.MOD_CONSTRUCTORS: "Mod constructors",
    Phase.STATIC_CONSTRUCTORS: "Static constructors",
    Phase.PATCHES: "Patches",
    Phase.DEFS: "Defs & XML",
    Phase.DEFERRED: "Deferred init",
    Phase.OTHER: "Other",
}

_EXACT: dict[str, Phase] = {
    "GlobalTextureAtlasManagerBakeStaticAtlases": Phase.CONTENT,
    "ModConstructor": Phase.MOD_CONSTRUCTORS,
    "StaticConstructorOnStartupUtilityCallAll": Phase.STATIC_CONSTRUCTORS,
    "ExecuteToExecuteWhenFinished": Phase.DEFERRED,
    "LoadDefs": Phase.DEFS,
    "LoadModXml": Phase.DEFS,
    "CombineXml": Phase.DEFS,
    "LoadedModManagerParseAndProcessXML": Phase.DEFS,
    "RegisterXmlInheritance": Phase.DEFS,
    "ResolveXmlInheritance": Phase.DEFS,
    "ClearCachedXmlInheritance": Phase.DEFS,
    "DefDatabaseAddAllInMods": Phase.DEFS,
    "ResolveReferences": Phase.DEFS,
    "ErrorCheckAllDefs": Phase.DEFS,
    "ShortHashGiverGiveAllShortHashes": Phase.DEFS,
}

_PREFIXES: tuple[tuple[str, Phase], ...] = (
    ("ModContentPackReloadContentInt", Phase.CONTENT),
    ("ResolveAllWantedCrossReferences", Phase.DEFS),
    ("DefGeneratorGenerateImpliedDefs", Phase.DEFS),
    ("DefOfHelperRebindAllDefOfs", Phase.DEFS),
)


def metric_phase(key: str) -> Phase:
    name = strip_metric_prefix(key).split("|", 1)[0]
    if phase := _EXACT.get(name):
        return phase
    if name.endswith("Patches"):
        return Phase.PATCHES
    for prefix, phase in _PREFIXES:
        if name.startswith(prefix):
            return phase
    return Phase.OTHER


def phase_totals(metrics: dict[str, float]) -> dict[Phase, float]:
    totals = dict.fromkeys(Phase, 0.0)
    for key, seconds in metrics.items():
        totals[metric_phase(key)] += seconds
    return totals
