#!/usr/bin/env python3
"""Rewrite the pxmodrim source tree to the new module layout.

Usage:
    python scripts/migrate-tree.py          # dry-run
    python scripts/migrate-tree.py --apply  # execute
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path
from typing import TypeAlias

ModulePath: TypeAlias = str

SRC_PKG = Path("src/pxmodrim")
TESTS_DIR = Path("tests")

MAPPING: dict[ModulePath, ModulePath] = {
    # Packages moving wholesale into core/
    "services": "core.services",
    "sort": "core.sort",
    "checker": "core.checker",
    "models": "core.models",
    # _compat/ dissolved into core/
    "_compat.config": "core.config",
    "_compat.constants": "core.constants",
    "_compat.utils": "core.utils",
    "_compat.xml": "core.xml",
    "_compat.mods_config": "core.mods_config",
    "_compat.mspec_hooks": "core.msgspec_hooks",
    "_compat.dialogs": "ui.components.dialogs",
    # ui/ subdirectory split
    "ui.main_window": "ui.window.main_window",
    "ui.menu_bar": "ui.window.menu_bar",
    "ui.about_panel": "ui.panels.about_panel",
    "ui.mod_info_panel": "ui.panels.mod_info_panel",
    "ui.mod_list_panel": "ui.panels.mod_list_panel",
    "ui.sidebar_panel": "ui.panels.sidebar_panel",
    "ui.settings_panel": "ui.panels.settings_panel",
    "ui.mod_list_model": "ui.models.mod_list_model",
    "ui.sidebar_model": "ui.models.sidebar_model",
    "ui.palette": "ui.theme.palette",
    "ui.theme": "ui.theme.qml_theme",
    "ui.constants": "ui.theme.constants",
}

FILE_MOVES: dict[str, str] = {
    "ui/style.qss": "ui/theme/style.qss",
}


IS_PACKAGE: set[ModulePath] = {
    "services",
    "sort",
    "checker",
    "models",
}


def _module_is_package(mod: ModulePath) -> bool:
    return mod in IS_PACKAGE


def _src_pkg_file(mod: ModulePath) -> Path:
    parts = mod.split(".")
    return SRC_PKG / "/".join(parts[:-1]) / f"{parts[-1]}.py"


def _src_pkg_dir(mod: ModulePath) -> Path:
    return SRC_PKG / mod.replace(".", "/")


def iter_py_files(*roots: Path) -> list[Path]:
    excluded_dirs = {"__pycache__"}
    files: list[Path] = []
    for root in roots:
        if not root.is_dir():
            continue
        for p in root.rglob("*.py"):
            if not any(part in excluded_dirs for part in p.relative_to(root).parts):
                files.append(p)
    return files


def build_plan() -> list[str]:
    plan: list[str] = []
    packages = [(o, n) for o, n in MAPPING.items() if o in IS_PACKAGE]
    modules = [(o, n) for o, n in MAPPING.items() if o not in IS_PACKAGE]

    for old_mod, new_mod in packages:
        plan.append(
            f"MOVE DIR  {SRC_PKG / old_mod.replace('.', '/')}/ → {_src_pkg_dir(new_mod)}/"
        )
    for old_mod, new_mod in modules:
        plan.append(
            f"MOVE FILE {_src_pkg_file(old_mod)} → {_src_pkg_file(new_mod)}"
        )
    for old_path, new_path in FILE_MOVES.items():
        plan.append(f"MOVE FILE {SRC_PKG / old_path} → {SRC_PKG / new_path}")

    py_files = iter_py_files(SRC_PKG, TESTS_DIR)
    moving_files = {
        _src_pkg_file(o) for o, _ in MAPPING.items() if not _module_is_package(o)
    }
    py_files = [p for p in py_files if p not in moving_files]
    plan.append("")
    plan.append(f"REWRITE imports in {len(py_files)} .py files")
    plan.append("")
    plan.append("DELETE empty directories")
    return plan


def _ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def _move_dir(old: Path, new: Path) -> None:
    _ensure_parent(new)
    if new.exists():
        shutil.rmtree(new)
    shutil.move(str(old), str(new))


def _move_file(old: Path, new: Path) -> None:
    _ensure_parent(new)
    shutil.move(str(old), str(new))


def _cleanup_empty_dirs(root: Path) -> None:
    for dirpath in sorted(root.rglob("*"), key=lambda p: len(str(p)), reverse=True):
        if dirpath.is_dir() and not any(dirpath.iterdir()):
            dirpath.rmdir()


def execute() -> None:
    # 1. Move packages
    for old_mod, new_mod in MAPPING.items():
        if old_mod not in IS_PACKAGE:
            continue
        old = _src_pkg_dir(old_mod)
        if not old.exists():
            print(f"  SKIP  {old}/ → {_src_pkg_dir(new_mod)}/ (already moved)")
            continue
        new = _src_pkg_dir(new_mod)
        print(f"  mv {old}/ → {new}/")
        _move_dir(old, new)

    # 2. Move individual module files
    for old_mod, new_mod in MAPPING.items():
        if old_mod in IS_PACKAGE:
            continue
        old = _src_pkg_file(old_mod)
        if not old.exists():
            print(f"  SKIP  {old} → {_src_pkg_file(new_mod)} (already moved)")
            continue
        new = _src_pkg_file(new_mod)
        print(f"  mv {old} → {new}")
        _move_file(old, new)

    # 3. Move non-Python files
    for old_rel, new_rel in FILE_MOVES.items():
        old = SRC_PKG / old_rel
        new = SRC_PKG / new_rel
        print(f"  mv {old} → {new}")
        _move_file(old, new)

    # 4. Delete unused mspec_hooks.py (old version)
    old_mspec = _src_pkg_file("core.mspec_hooks")
    if old_mspec.exists():
        print(f"  rm {old_mspec}")
        old_mspec.unlink()

    # 5. Clean up empty dirs
    _cleanup_empty_dirs(SRC_PKG)

    # 6. Rewrite imports
    _rewrite_imports()

    print("\nDone. Run 'just check' to verify.")


def _rewrite_imports() -> None:
    replacements = sorted(
        ((o, n) for o, n in MAPPING.items()),
        key=lambda x: len(x[0]),
        reverse=True,
    )

    files = iter_py_files(SRC_PKG, TESTS_DIR)
    changed = 0

    for filepath in files:
        try:
            original = filepath.read_text(encoding="utf-8")
        except Exception as exc:
            print(f"  WARN cannot read {filepath}: {exc}", file=sys.stderr)
            continue

        new_text = original
        for old_mod, new_mod in replacements:
            new_text = re.sub(
                rf"\bfrom\s+pxmodrim\.{re.escape(old_mod)}(?![.\w])",
                lambda m, nm=new_mod: f"from pxmodrim.{nm}",
                new_text,
            )
            new_text = re.sub(
                rf"\bimport\s+pxmodrim\.{re.escape(old_mod)}(?![.\w])",
                lambda m, nm=new_mod: f"import pxmodrim.{nm}",
                new_text,
            )

        if new_text != original:
            filepath.write_text(new_text, encoding="utf-8")
            changed += 1

    print(f"\nRewrote imports in {changed} files")


def main() -> int:
    parser = argparse.ArgumentParser(description="Migrate pxmodrim module layout")
    parser.add_argument("--apply", action="store_true", help="Execute the migration")
    args = parser.parse_args()

    if args.apply:
        print("Executing migration...\n")
        execute()
    else:
        print("Dry-run plan:\n")
        for line in build_plan():
            print(f"  {line}")
        print("\nPass --apply to execute.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
