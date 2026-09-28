from __future__ import annotations

import argparse
import csv
import xml.etree.ElementTree as ET
from pathlib import Path

DEFAULT_MODS = Path.home() / "Games/RimWorld/RimWorld/game/Mods"
RULES = (
    ("Official", "prefix", "ludeon.rimworld"),
    ("Frameworks & Libraries", "exact", "brrainz.harmony"),
    ("Frameworks & Libraries", "exact", "unlimitedhugs.hugslib"),
    ("Frameworks & Libraries", "exact", "erdelf.humanoidalienraces"),
    ("Frameworks & Libraries", "exact", "oskarpotocki.vanillafactionsexpanded.core"),
    ("Frameworks & Libraries", "exact", "imranfish.xmlextensions"),
    ("Frameworks & Libraries", "exact", "zetrith.prepatcher"),
    ("Frameworks & Libraries", "exact", "jecrell.jecstools"),
    ("Frameworks & Libraries", "exact", "bs.fishery"),
    ("Frameworks & Libraries", "exact", "smashphil.vehicleframework"),
    ("Performance", "exact", "bs.performance"),
    ("Performance", "exact", "krkr.rocketman"),
    ("Performance", "exact", "taranchuk.performanceoptimizer"),
    ("Performance", "prefix", "dubwise.dubsperformanceanalyzer"),
    ("Quality of Life", "exact", "brrainz.achtung"),
    ("Quality of Life", "exact", "brrainz.cameraplus"),
    ("Quality of Life", "exact", "unlimitedhugs.allowtool"),
    ("Quality of Life", "exact", "jaxe.rimhud"),
    ("Quality of Life", "exact", "mehni.pickupandhaul"),
    ("Quality of Life", "exact", "avilmask.commonsense"),
    ("Quality of Life", "exact", "dhultgren.smarterconstruction"),
    ("Quality of Life", "exact", "roolo.searchanddestroy"),
    ("Vanilla Expanded", "prefix", "vanillaexpanded."),
    ("Vanilla Expanded", "prefix", "oskarpotocki."),
    ("Vanilla Expanded", "prefix", "vanillaracesexpanded."),
    ("Vanilla Expanded", "prefix", "vanillaquestsexpanded."),
    ("Vanilla Expanded", "prefix", "vanillastorytellersexpanded."),
    ("Alpha Mods", "prefix", "sarg."),
    ("Combat Extended", "prefix", "ceteam."),
    ("Dubs Mods", "prefix", "dubwise."),
)


def read_mod(folder: Path) -> dict[str, str] | None:
    about = folder / "About" / "About.xml"
    if not about.is_file():
        return None
    try:
        root = ET.parse(about).getroot()
    except (ET.ParseError, OSError):
        return None

    def value(tag: str) -> str:
        element = root.find(tag)
        return " ".join((element.text or "").split()) if element is not None else ""

    return {
        "folder": folder.name,
        "name": value("name"),
        "package_id": value("packageId"),
        "author": value("author"),
        "description": value("description"),
    }


def classify(mod: dict[str, str]) -> str:
    package_id = mod["package_id"].casefold()
    for category, operation, pattern in RULES:
        if operation == "exact" and package_id == pattern:
            return category
        if operation == "prefix" and package_id.startswith(pattern):
            return category
    if "combat extended" in mod["name"].casefold():
        return "Combat Extended"
    if "oskar potocki" in mod["author"].casefold():
        return "Vanilla Expanded"
    return ""


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Export installed mods, assigning only recognized default categories."
    )
    parser.add_argument("mods", nargs="?", type=Path, default=DEFAULT_MODS)
    parser.add_argument("output", nargs="?", type=Path, default=Path("installed-mods.csv"))
    args = parser.parse_args()

    rows = [
        mod
        for folder in sorted(args.mods.iterdir())
        if folder.is_dir()
        if (mod := read_mod(folder)) is not None
    ]
    for mod in rows:
        mod["category"] = classify(mod)
    rows.sort(key=lambda mod: (mod["category"] or "Unclassified", mod["name"].casefold()))
    fields = ("category", "name", "package_id", "author", "description", "folder")
    with args.output.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows({key: row[key] for key in fields} for row in rows)

    counts: dict[str, int] = {}
    for mod in rows:
        category = mod["category"] or "Unclassified"
        counts[category] = counts.get(category, 0) + 1
    print(f"Exported {len(rows)} mods to {args.output}")
    for category, count in sorted(counts.items()):
        print(f"{category}: {count}")


if __name__ == "__main__":
    main()
