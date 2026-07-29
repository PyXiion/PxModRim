from __future__ import annotations

import platform
import shutil
import subprocess
import sys
from pathlib import Path


def get_standalone_args(release: bool = False, bundle_qt: bool = True) -> list[str]:
    project_root = Path(__file__).parent.parent

    args = [
        sys.executable,
        "-m",
        "nuitka",
        "--standalone",
        "--enable-plugin=pyside6",
        "--nofollow-import-to=pytest,pygments",
        "--include-package-data=pxmodrim",
        f"--output-dir={project_root / 'dist'}",
        "--assume-yes-for-downloads",
        str(project_root / "packaging" / "entrypoint.py"),
    ]

    args.append("--include-qt-plugins=qml")

    if release:
        args.extend([
            "--lto=yes",
            "--python-flag=no_asserts",
            "--python-flag=no_docstrings",
        ])

    args.extend([
        "--noinclude-setuptools-mode=nofollow",
        "--noinclude-pytest-mode=nofollow",
        "--noinclude-unittest-mode=nofollow",
        "--noinclude-default-mode=nofollow",
        "--nowarn-mnemonic=unwanted-module",
    ])

    system = platform.system()
    if system == "Windows":
        import tomllib
        pyproject = project_root / "pyproject.toml"
        version = tomllib.loads(pyproject.read_text("utf-8"))["project"]["version"]
        args.extend([
            "--windows-icon-from-ico=packaging/logo.ico",
            "--windows-company-name=PxModRim",
            "--windows-product-name=PxModRim",
            f"--windows-product-version={version}",
        ])

    return args


def _clean_debug_artifacts(base_dir: Path) -> None:
    for obj_dir in base_dir.rglob("objects-RelWithDebInfo"):
        shutil.rmtree(obj_dir)
        print(f"Removed {obj_dir}")


def clean_qml_debug_artifacts() -> None:
    for qml_dir in Path(".venv").rglob("**/PySide6/Qt/qml"):
        _clean_debug_artifacts(qml_dir)


def copy_missing_libs(project_root: Path) -> None:
    dist_dir = project_root / "dist" / "entrypoint.dist"

    qml_dir = dist_dir / "PySide6" / "qml"
    if qml_dir.exists():
        _clean_debug_artifacts(qml_dir)

    venv_qml = None
    for pattern in [
        ".venv/**/PySide6/Qt/qml",
        "venv/**/PySide6/Qt/qml",
    ]:
        matches = list(project_root.glob(pattern))
        if matches:
            venv_qml = matches[0]
            break

    if not venv_qml:
        print("Warning: PySide6 QML not found in venv, skipping")
        return

    venv_lib = venv_qml.parent / "lib"

    required_libs = [
        "libQt6QmlModels.so.6",
        "libQt6QuickTemplates2.so.6",
        "libQt6QuickControls2.so.6",
        "libQt6QuickControls2Impl.so.6",
        "libQt6QuickLayouts.so.6",
    ]

    for lib_name in required_libs:
        src = venv_lib / lib_name
        dst = dist_dir / lib_name
        if src.exists() and not dst.exists():
            shutil.copy2(src, dst)
            print(f"Copied {lib_name}")


def create_appimage(project_root: Path) -> None:
    dist_dir = project_root / "dist" / "entrypoint.dist"
    app_dir = project_root / "dist" / "PxModRim.AppDir"

    if app_dir.exists():
        shutil.rmtree(app_dir)

    shutil.copytree(dist_dir, app_dir)

    binary = app_dir / "PxModRim"
    if not binary.exists():
        old_binary = app_dir / "entrypoint.bin"
        if old_binary.exists():
            old_binary.rename(binary)

    desktop_content = """[Desktop Entry]
Type=Application
Name=PxModRim
Comment=Mod manager for RimWorld
Icon=pxmodrim
Exec=PxModRim
Categories=Game;
StartupWMClass=PxModRim
Terminal=false
"""
    (app_dir / "pxmodrim.desktop").write_text(desktop_content)

    icon_src = project_root / "src" / "pxmodrim" / "ui" / "assets" / "logo.svg"
    shutil.copy(icon_src, app_dir / "pxmodrim.svg")

    run_script = """#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/PxModRim" "$@"
"""
    run_file = app_dir / "AppRun"
    run_file.write_text(run_script)
    run_file.chmod(0o755)

    appimagetool = shutil.which("appimagetool")
    if not appimagetool:
        print("Warning: appimagetool not found, skipping AppImage creation")
        print("Install from: https://github.com/AppImage/AppImageKit/releases")
        return

    output = project_root / "dist" / "PxModRim-x86_64.AppImage"
    subprocess.run([appimagetool, str(app_dir), str(output)], check=True)
    print(f"Created {output}")


def strip_unused_qml_modules(dist_dir: Path) -> None:
    """Remove QML modules not imported anywhere in the app's .qml files.

    The app only uses QtQuick, QtQuick.Controls, QtQuick.Layouts,
    QtWebChannel, and QtWebEngine.
    """
    qml_root = dist_dir / "PySide6" / "qml"
    keep = {"Qt", "QtQml", "QtQuick", "QtWebChannel", "QtWebEngine"}
    removed = 0
    for entry in sorted(qml_root.iterdir()):
        if entry.is_dir() and entry.name not in keep:
            size = sum(f.stat().st_size for f in entry.rglob("*") if f.is_file())
            shutil.rmtree(entry)
            removed += size
            if size > 1024 * 1024:
                print(f"  Removed {entry.name} ({size // (1024 * 1024)} MB)")
            else:
                print(f"  Removed {entry.name} ({size // 1024} KB)")
    if removed:
        print(f"  Freed {removed // (1024 * 1024)} MB by removing unused QML modules")


def strip_qt_translations(dist_dir: Path) -> None:
    """Remove all Qt .qm files except English ones."""
    removed = 0
    for f in dist_dir.glob("*.qm"):
        base = f.stem.lower()
        if any(base.endswith(suf) for suf in ("_en", "_en_us", "_en_gb")):
            continue
        removed += f.stat().st_size
        f.unlink()
    if removed:
        print(f"  Removed non-English .qm files ({removed // 1024} KB)")


def strip_qt_devtools(dist_dir: Path) -> None:
    """Remove QtWebEngine DevTools resources (~12 MB)."""
    pak = dist_dir / "qtwebengine_devtools_resources.pak"
    if pak.exists():
        sz = pak.stat().st_size
        pak.unlink()
        print(f"  Removed qtwebengine_devtools_resources.pak ({sz // (1024 * 1024)} MB)")


def strip_qt_locales(dist_dir: Path) -> None:
    """Keep only en-US WebEngine locale, remove the other 50+."""
    locale_dir = dist_dir / "qtwebengine_locales"
    if not locale_dir.is_dir():
        return
    kept = en_count = 0
    for f in locale_dir.iterdir():
        if f.suffix != ".pak":
            continue
        if f.stem in ("en-US", "en-GB", "en"):
            en_count += 1
            continue
        f.unlink()
        kept += 1
    print(f"  Kept {en_count} English locale(s), removed {kept} other locales")


def strip_bundled_qt(dist_dir: Path) -> None:
    """Remove bundled Qt shared libs so the app uses system Qt at runtime."""
    patterns = [
        "libQt6*.so*",
        "libicu*.so*",
        "icudtl.dat",
    ]
    total = 0
    for pattern in patterns:
        for f in dist_dir.glob(pattern):
            if f.is_file():
                size = f.stat().st_size
                f.unlink()
                total += size
                print(f"  Removed {f.name} ({size // 1024 // 1024}MB)")
    print(f"  Freed {total // 1024 // 1024}MB by removing bundled Qt libs")


def _get_dir_size(path: Path) -> int:
    total = 0
    for entry in path.rglob("*"):
        if entry.is_file():
            total += entry.stat().st_size
    return total


def main() -> None:
    release = "--release" in sys.argv
    bundle_qt = "--bundle-qt" in sys.argv
    project_root = Path(__file__).parent.parent
    system = platform.system()

    print("Step 0: Cleaning QML debug artifacts...")
    clean_qml_debug_artifacts()

    print(f"Step 1: Building standalone (release={release}, bundle_qt={bundle_qt})...")
    args = get_standalone_args(release=release, bundle_qt=bundle_qt)
    print(f"Running: {' '.join(args)}")

    result = subprocess.run(args, check=False)
    if result.returncode != 0:
        sys.exit(result.returncode)

    dist_dir = project_root / "dist" / "entrypoint.dist"

    print("\nStep 2a: Stripping non-English WebEngine locales...")
    strip_qt_locales(dist_dir)

    print("\nStep 2b: Stripping unused QML modules...")
    strip_unused_qml_modules(dist_dir)

    print("\nStep 2c: Stripping Qt translations...")
    strip_qt_translations(dist_dir)

    print("\nStep 2d: Stripping QtWebEngine DevTools...")
    strip_qt_devtools(dist_dir)

    if bundle_qt:
        print("\nStep 2e: Copying missing Qt libraries...")
        copy_missing_libs(project_root)
    else:
        print("\nStep 2e: Stripping bundled Qt libs (using system Qt)...")
        strip_bundled_qt(dist_dir)

    output_name = "PxModRim"
    if system == "Windows":
        output_name += ".exe"

    binary = dist_dir / "entrypoint.bin"
    if binary.exists():
        final_binary = dist_dir / output_name
        binary.rename(final_binary)
        print(f"Renamed to {final_binary}")

    if system == "Linux":
        if bundle_qt:
            print("\nStep 3: Creating AppImage...")
            create_appimage(project_root)
        else:
            print("\nStep 3: Skipping AppImage (system Qt mode, not self-contained)")

    print(f"\nBuild complete! Output: {dist_dir}")
    print(f"Run with: {dist_dir / output_name}")

    size = _get_dir_size(dist_dir)
    print(f"Build size: {size // (1024 * 1024)} MB ({size:,} bytes)")


if __name__ == "__main__":
    main()
