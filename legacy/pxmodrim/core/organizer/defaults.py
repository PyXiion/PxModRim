from __future__ import annotations

import msgspec

from pxmodrim.core.organizer.models import RuleField, RuleOp


class StandardRule(msgspec.Struct, frozen=True):
    """A built-in auto-folder rule targeting a top-level folder by name."""

    folder: str
    field: RuleField
    op: RuleOp
    pattern: str


def _exact(folder: str, *package_ids: str) -> tuple[StandardRule, ...]:
    return tuple(StandardRule(folder, "package_id", "equals", p) for p in package_ids)


def _prefixes(folder: str, *prefixes: str) -> tuple[StandardRule, ...]:
    return tuple(StandardRule(folder, "package_id", "prefix", p) for p in prefixes)


# Package ids are taken from each mod's own About.xml. Multi-category authors
# (brrainz, unlimitedhugs, bs, mlie) are matched by exact id only, and name
# heuristics such as "framework" or "performance" are avoided: they catch
# translations and unrelated content mods. Order matters (first match wins):
# libraries and performance precede the family prefixes that would swallow
# VE Framework and Dubs Performance Analyzer.
STANDARD_RULES: tuple[StandardRule, ...] = (
    *_prefixes("Official", "ludeon.rimworld"),
    *_exact(
        "Frameworks & Libraries",
        "brrainz.harmony",
        "unlimitedhugs.hugslib",
        "erdelf.humanoidalienraces",
        "oskarpotocki.vanillafactionsexpanded.core",
        "imranfish.xmlextensions",
        "zetrith.prepatcher",
        "jecrell.jecstools",
        "bs.fishery",
        "smashphil.vehicleframework",
    ),
    *_exact(
        "Performance",
        "bs.performance",
        "krkr.rocketman",
        "taranchuk.performanceoptimizer",
    ),
    *_prefixes("Performance", "dubwise.dubsperformanceanalyzer"),
    *_exact(
        "Quality of Life",
        "brrainz.achtung",
        "brrainz.cameraplus",
        "unlimitedhugs.allowtool",
        "jaxe.rimhud",
        "mehni.pickupandhaul",
        "avilmask.commonsense",
        "dhultgren.smarterconstruction",
        "roolo.searchanddestroy",
    ),
    *_prefixes(
        "Vanilla Expanded",
        "vanillaexpanded.",
        "oskarpotocki.",
        "vanillaracesexpanded.",
        "vanillaquestsexpanded.",
        "vanillastorytellersexpanded.",
    ),
    StandardRule("Vanilla Expanded", "author", "contains", "oskar potocki"),
    *_prefixes("Alpha Mods", "sarg."),
    *_prefixes("Combat Extended", "ceteam."),
    StandardRule("Combat Extended", "name", "contains", "combat extended"),
    *_prefixes("Dubs Mods", "dubwise."),
)
