from __future__ import annotations

import platform
import shutil
import subprocess
import sys
from pathlib import Path


def get_standalone_args(release: bool = False) -> list[str]:
    project_root = Path(__file__).parent.parent

    args = [
        sys.executable,
        "-m",
        "nuitka",
        "--standalone",
        "--enable-plugin=pyside6",
        "--include-package-data=pxmodrim",
        f"--output-dir={project_root / 'dist'}",
        "--assume-yes-for-downloads",
        str(project_root / "packaging" / "entrypoint.py"),
    ]

    if release:
        args.append("--lto=yes")

    system = platform.system()
    if system == "Windows":
        args.extend([
            "--windows-icon-from-ico=packaging/logo.ico",
            "--windows-company-name=PxModRim",
            "--windows-product-name=PxModRim",
        ])

    return args


def copy_qml_plugins(project_root: Path) -> None:
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

    dist_qml = project_root / "dist" / "entrypoint.dist" / "PySide6" / "Qt" / "qml"
    if dist_qml.exists():
        shutil.rmtree(dist_qml)
    dist_qml.parent.mkdir(parents=True, exist_ok=True)

    shutil.copytree(venv_qml, dist_qml)
    print(f"Copied QML plugins from {venv_qml} to {dist_qml}")

    for obj_dir in dist_qml.rglob("objects-RelWithDebInfo"):
        shutil.rmtree(obj_dir)
        print(f"Removed {obj_dir}")

    venv_lib = venv_qml.parent / "lib"
    dist_dir = project_root / "dist" / "entrypoint.dist"
    
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


def main() -> None:
    release = "--release" in sys.argv
    project_root = Path(__file__).parent.parent
    system = platform.system()

    print(f"Step 1: Building standalone (release={release})...")
    args = get_standalone_args(release=release)
    print(f"Running: {' '.join(args)}")

    result = subprocess.run(args, check=False)
    if result.returncode != 0:
        sys.exit(result.returncode)

    print("\nStep 2: Copying QML plugins and missing libraries...")
    copy_qml_plugins(project_root)

    dist_dir = project_root / "dist" / "entrypoint.dist"
    output_name = "PxModRim"
    if system == "Windows":
        output_name += ".exe"
    
    binary = dist_dir / "entrypoint.bin"
    if binary.exists():
        final_binary = dist_dir / output_name
        binary.rename(final_binary)
        print(f"Renamed to {final_binary}")

    if system == "Linux":
        print("\nStep 3: Creating AppImage...")
        create_appimage(project_root)

    print(f"\nBuild complete! Output: {dist_dir}")
    print(f"Run with: {dist_dir / output_name}")


if __name__ == "__main__":
    main()
