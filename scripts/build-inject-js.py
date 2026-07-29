from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
TS_DIR = (
    PROJECT_ROOT
    / "src"
    / "pxmodrim"
    / "ui"
    / "plugins"
    / "steam_workshop"
    / "ts"
)
OUT_FILE = (
    PROJECT_ROOT
    / "src"
    / "pxmodrim"
    / "ui"
    / "plugins"
    / "steam_workshop"
    / "inject.js"
)


def main() -> int:
    sources = list(TS_DIR.rglob("*.ts"))
    if not sources:
        print("No TypeScript source files found in", TS_DIR)
        return 1

    latest_source = max(s.stat().st_mtime for s in sources)

    if OUT_FILE.exists() and OUT_FILE.stat().st_mtime >= latest_source:
        print(f"{OUT_FILE} is up to date")
        return 0

    npx = shutil.which("npx")
    if npx is None:
        print("npx not found — install Node.js or pre-build inject.js")
        return 1

    print(f"Building {OUT_FILE} from {len(sources)} source files...")
    result = subprocess.run(
        [
            npx,
            "--yes",
            "esbuild",
            str(TS_DIR / "main.ts"),
            "--bundle",
            "--format=iife",
            f"--outfile={OUT_FILE}",
            "--target=es2020",
        ],
        cwd=PROJECT_ROOT,
        check=False,
    )

    if result.returncode != 0:
        print(f"esbuild failed with exit code {result.returncode}")
        return result.returncode

    print(f"Built {OUT_FILE}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
