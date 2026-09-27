from __future__ import annotations

import msgspec

from pxmodrim.core.organizer.models import RuleField, RuleOp


class StandardRule(msgspec.Struct, frozen=True):
    """A built-in auto-folder rule targeting a top-level folder by name."""

    folder: str
    field: RuleField
    op: RuleOp
    pattern: str


STANDARD_RULES: tuple[StandardRule, ...] = (
    StandardRule("Official", "package_id", "prefix", "ludeon.rimworld"),
    StandardRule("Frameworks & Libraries", "package_id", "equals", "brrainz.harmony"),
    StandardRule(
        "Frameworks & Libraries", "package_id", "equals", "unlimitedhugs.hugslib"
    ),
    StandardRule(
        "Frameworks & Libraries", "package_id", "equals", "erdelf.humanoidalienraces"
    ),
    StandardRule("Frameworks & Libraries", "name", "contains", "framework"),
    StandardRule("Vanilla Expanded", "author", "contains", "oskar potocki"),
    StandardRule("Vanilla Expanded", "package_id", "prefix", "oskarpotocki."),
    StandardRule("Vanilla Expanded", "package_id", "prefix", "vanillaexpanded."),
    StandardRule("Combat Extended", "package_id", "prefix", "ceteam."),
    StandardRule("Combat Extended", "name", "contains", "combat extended"),
    StandardRule("Alpha Mods", "package_id", "prefix", "sarg."),
    StandardRule("Performance", "name", "contains", "performance"),
    StandardRule("Performance", "name", "contains", "rocketman"),
)
