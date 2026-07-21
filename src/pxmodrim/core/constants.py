from __future__ import annotations

from enum import IntEnum

RIMWORLD_STEAM_APP_ID = "294100"


class LaunchStrategy(IntEnum):
    DIRECT = 0
    STEAM = 1
DEFAULT_MISSING_PACKAGEID = "missing.packageid"

RIMWORLD_DLC_METADATA: dict[str, dict[str, str]] = {
    "294100": {
        "packageid": "ludeon.rimworld",
        "name": "RimWorld",
        "steam_url": "https://store.steampowered.com/app/294100/RimWorld",
        "description": "Base game",
    },
    "1149640": {
        "packageid": "ludeon.rimworld.royalty",
        "name": "RimWorld - Royalty",
        "steam_url": "https://store.steampowered.com/app/1149640/RimWorld__Royalty",
        "description": "DLC #1",
    },
    "1392840": {
        "packageid": "ludeon.rimworld.ideology",
        "name": "RimWorld - Ideology",
        "steam_url": "https://store.steampowered.com/app/1392840/RimWorld__Ideology",
        "description": "DLC #2",
    },
    "1826140": {
        "packageid": "ludeon.rimworld.biotech",
        "name": "RimWorld - Biotech",
        "steam_url": "https://store.steampowered.com/app/1826140/RimWorld__Biotech",
        "description": "DLC #3",
    },
    "2380740": {
        "packageid": "ludeon.rimworld.anomaly",
        "name": "RimWorld - Anomaly",
        "steam_url": "https://store.steampowered.com/app/2380740/RimWorld__Anomaly/",
        "description": "DLC #4",
    },
    "3022790": {
        "packageid": "ludeon.rimworld.odyssey",
        "name": "RimWorld - Odyssey",
        "steam_url": "https://store.steampowered.com/app/3022790/RimWorld__Odyssey/",
        "description": "DLC #5",
    },
}

RIMWORLD_PACKAGE_IDS: list[str] = [
    v["packageid"] for v in RIMWORLD_DLC_METADATA.values()
]
