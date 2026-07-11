from __future__ import annotations

from pathlib import Path


def find_about_xml(mod_path: Path) -> Path | None:
    """Find About.xml in a mod directory (case-insensitive)."""
    if not mod_path.is_dir():
        return None
    for entry in mod_path.iterdir():
        if entry.name.lower() == "about" and entry.is_dir():
            for child in entry.iterdir():
                if child.name.lower() == "about.xml" and child.is_file():
                    return child
            break
    return None