from __future__ import annotations

import random
import string
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

from loguru import logger

logger.remove()

from pxmodrim.core.models.metadata.parsing import create_listed_mod_from_path
from pxmodrim.core.services.mod_discovery import scan_mod_directory


ABOUT_XML = """\
<?xml version="1.0" encoding="utf-8"?>
<ModMetaData>
  <name>{name}</name>
  <author>{author}</author>
  <packageId>{author}.{name}</packageId>
  <url>https://steamcommunity.com/sharedfiles/filedetails/?id={sid}</url>
  <supportedVersions>
    <li>1.4</li>
    <li>1.5</li>
  </supportedVersions>
  <loadAfter>
    <li>{dep}</li>
  </loadAfter>
  <description>A benchmark mod #{i}</description>
  <modVersion>1.0.0</modVersion>
</ModMetaData>"""


def make_mod_dir(root: Path, i: int, dep_pid: str) -> Path:
    author = random.choice(string.ascii_lowercase[:5])
    name = f"benchmark_mod_{i:04d}"
    pid = f"{author}.{name}"

    about = ABOUT_XML.format(
        name=name, author=author, sid=i, dep=dep_pid, i=i
    )

    mod_dir = root / name
    about_dir = mod_dir / "About"
    about_dir.mkdir(parents=True, exist_ok=True)
    (about_dir / "About.xml").write_text(about)
    (about_dir / "PublishedFileId.txt").write_text(str(i))
    return mod_dir


def make_mods(root: Path, count: int) -> None:
    """Create count mod directories with dependency chains."""
    prev_pid = "ludeon.rimworld"
    for i in range(count):
        make_mod_dir(root, i, prev_pid)
        author = random.choice(string.ascii_lowercase[:5])
        prev_pid = f"{author}.benchmark_mod_{i:04d}"


def bench(count: int) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp) / "Mods"
        root.mkdir()
        make_mods(root, count)

        t0 = time.perf_counter_ns()
        scanned = scan_mod_directory(root)
        t1 = time.perf_counter_ns()

        mods = {}
        t2 = time.perf_counter_ns()
        for d, about in scanned.items():
            _, mod = create_listed_mod_from_path(
                d, "1.5", about_xml_path=about
            )
            mods[mod.uuid] = mod
        t3 = time.perf_counter_ns()

    scan_ms = (t1 - t0) / 1e6
    parse_ms = (t3 - t2) / 1e6
    total_ms = (t3 - t0) / 1e6
    print(f"{count:>5} mods: scan_dir={scan_ms:>6.1f}ms  parse_xml={parse_ms:>6.1f}ms  total={total_ms:>6.1f}ms")
    print(f"  scan avg: {scan_ms/count*1000:.1f} us/mod  parse avg: {parse_ms/count*1000:.1f} us/mod")


if __name__ == "__main__":
    for count in (50, 100, 200, 500, 1000, 2000):
        bench(count)
