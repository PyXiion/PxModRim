from __future__ import annotations

import os
import platform
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path


def get_version(project_root: Path) -> str:
    tag = os.environ.get("GITHUB_REF_NAME", "")
    if tag.startswith("v") and len(tag) > 1 and tag[1].isdigit():
        return tag[1:]
    pyproject = project_root / "pyproject.toml"
    return tomllib.loads(pyproject.read_text("utf-8"))["project"]["version"]


def get_standalone_args(release: bool = False) -> list[str]:
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
        args.extend(
            [
                "--lto=yes",
                "--python-flag=no_asserts",
                "--python-flag=no_docstrings",
            ]
        )

    args.extend(
        [
            "--noinclude-setuptools-mode=nofollow",
            "--noinclude-pytest-mode=nofollow",
            "--noinclude-unittest-mode=nofollow",
            "--noinclude-default-mode=nofollow",
            "--nowarn-mnemonic=unwanted-module",
        ]
    )

    system = platform.system()
    version = get_version(project_root)
    if system == "Windows":
        args.extend(
            [
                "--windows-icon-from-ico=packaging/logo.ico",
                "--windows-company-name=PxModRim",
                "--windows-product-name=PxModRim",
                f"--windows-product-version={version}",
            ]
        )
    elif system == "Darwin":
        logo_icns = project_root / "packaging" / "logo.icns"
        args.extend(
            [
                "--macos-create-app-bundle",
                "--macos-app-name=PxModRim",
                f"--macos-app-version={version}",
                "--macos-signed-app-name=com.github.PyXiion.PxModRim",
                f"--macos-app-icon={logo_icns}",
            ]
        )

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


def normalize_executable(dist_dir: Path, system: str | None = None) -> Path:
    """Normalize the Nuitka entrypoint binary to PxModRim (or PxModRim.exe on Windows).

    Searches platform-appropriate candidate names, renames the first matching
    source executable to the final target name, and fails clearly if neither
    the final executable nor any source candidate exists.
    """
    if system is None:
        system = platform.system()

    output_name = "PxModRim.exe" if system == "Windows" else "PxModRim"
    final_binary = dist_dir / output_name

    if final_binary.is_file():
        return final_binary

    candidates = (
        ["entrypoint.exe", "entrypoint.bin", "entrypoint"]
        if system == "Windows"
        else ["entrypoint", "entrypoint.bin", "entrypoint.exe"]
    )

    for candidate_name in candidates:
        candidate_path = dist_dir / candidate_name
        if candidate_path.is_file():
            candidate_path.rename(final_binary)
            print(f"Renamed {candidate_path.name} to {final_binary.name}")
            return final_binary

    raise FileNotFoundError(
        f"Executable not found in {dist_dir}; looked for '{output_name}' "
        f"and candidates {candidates}"
    )


def create_appimage(project_root: Path) -> None:
    dist_dir = project_root / "dist" / "entrypoint.dist"
    app_dir = project_root / "dist" / "PxModRim.AppDir"

    if app_dir.exists():
        shutil.rmtree(app_dir)

    shutil.copytree(dist_dir, app_dir)

    normalize_executable(app_dir, system="Linux")

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


def create_deb(project_root: Path) -> Path | None:
    dist_dir = project_root / "dist" / "entrypoint.dist"
    if not dist_dir.exists():
        print("Warning: entrypoint.dist not found, skipping .deb creation")
        return None

    dpkg_deb = shutil.which("dpkg-deb")
    if not dpkg_deb:
        raise RuntimeError("dpkg-deb is required to create the Debian package")

    version = get_version(project_root)
    output = project_root / "dist" / f"pxmodrim_{version}_amd64.deb"
    staging_dir = project_root / "dist" / "deb_staging"
    if staging_dir.exists():
        shutil.rmtree(staging_dir)

    debian_dir = staging_dir / "DEBIAN"
    opt_dir = staging_dir / "opt" / "pxmodrim"
    bin_dir = staging_dir / "usr" / "bin"
    apps_dir = staging_dir / "usr" / "share" / "applications"
    icons_dir = (
        staging_dir / "usr" / "share" / "icons" / "hicolor" / "scalable" / "apps"
    )

    debian_dir.mkdir(parents=True, exist_ok=True)
    bin_dir.mkdir(parents=True, exist_ok=True)
    apps_dir.mkdir(parents=True, exist_ok=True)
    icons_dir.mkdir(parents=True, exist_ok=True)

    shutil.copytree(dist_dir, opt_dir)
    (bin_dir / "pxmodrim").symlink_to("/opt/pxmodrim/PxModRim")
    shutil.copy2(
        project_root / "packaging" / "linux" / "pxmodrim.desktop",
        apps_dir / "pxmodrim.desktop",
    )
    shutil.copy2(
        project_root / "src" / "pxmodrim" / "ui" / "assets" / "logo.svg",
        icons_dir / "pxmodrim.svg",
    )

    exe_file = opt_dir / "PxModRim"
    if exe_file.exists():
        exe_file.chmod(0o755)

    installed_size = (
        sum(f.stat().st_size for f in staging_dir.rglob("*") if f.is_file()) // 1024
    )
    control_content = f"""Package: pxmodrim
Version: {version}
Section: games
Priority: optional
Architecture: amd64
Installed-Size: {installed_size}
Maintainer: PyXiion
Homepage: https://github.com/PyXiion/PxModRim
Description: Mod manager for RimWorld
 PxModRim is a fast and modern mod manager for RimWorld.
"""
    (debian_dir / "control").write_text(control_content, encoding="utf-8")

    subprocess.run(
        [dpkg_deb, "--build", "--root-owner-group", str(staging_dir), str(output)],
        check=True,
    )
    shutil.rmtree(staging_dir)
    print(f"Created {output}")
    return output


def create_rpm(project_root: Path) -> Path | None:
    rpmbuild = shutil.which("rpmbuild")
    if not rpmbuild:
        print("Warning: rpmbuild not found, skipping RPM creation")
        return None

    version = get_version(project_root)
    dist_dir = project_root / "dist" / "entrypoint.dist"
    if not dist_dir.exists():
        print("Warning: entrypoint.dist not found, skipping RPM creation")
        return None

    top_dir = project_root / "dist" / "rpmbuild"
    if top_dir.exists():
        shutil.rmtree(top_dir)
    for sub in ("BUILD", "RPMS", "SOURCES", "SPECS", "SRPMS"):
        (top_dir / sub).mkdir(parents=True, exist_ok=True)

    spec_file = project_root / "packaging" / "linux" / "pxmodrim.spec"
    cmd = [
        rpmbuild,
        "-bb",
        f"--define=_topdir {top_dir.resolve()}",
        f"--define=project_root {project_root.resolve()}",
        f"--define=version {version}",
        str(spec_file.resolve()),
    ]
    subprocess.run(cmd, check=True)

    rpm_files = list((top_dir / "RPMS").rglob("*.rpm"))
    if not rpm_files:
        print("Warning: No RPM file was generated")
        return None

    output = project_root / "dist" / f"pxmodrim-{version}-1.x86_64.rpm"
    shutil.copy2(rpm_files[0], output)
    shutil.rmtree(top_dir)
    print(f"Created {output}")
    return output


def create_nsis_installer(project_root: Path) -> Path | None:
    makensis = shutil.which("makensis")
    nsis_dir = os.environ.get("NSIS_DIR")
    if nsis_dir:
        configured_nsis = Path(nsis_dir) / "makensis.exe"
        if configured_nsis.exists():
            makensis = str(configured_nsis)
    if not makensis and platform.system() == "Windows":
        default_nsis = Path("C:/Program Files (x86)/NSIS/makensis.exe")
        if default_nsis.exists():
            makensis = str(default_nsis)

    if not makensis:
        print("Warning: makensis not found, skipping NSIS installer creation")
        return None

    version = get_version(project_root)
    dist_dir = project_root / "dist" / "entrypoint.dist"
    output = project_root / "dist" / "PxModRim-Setup.exe"
    nsi_file = project_root / "packaging" / "windows" / "installer.nsi"

    cmd = [
        makensis,
        f"/DVERSION={version}",
        f"/DDIST_DIR={dist_dir.resolve()}",
        f"/DOUTPUT_FILE={output.resolve()}",
        str(nsi_file.resolve()),
    ]
    subprocess.run(cmd, check=True)
    print(f"Created {output}")
    return output


def create_macos_bundle(project_root: Path) -> Path | None:
    version = get_version(project_root)
    dist_path = project_root / "dist"

    app_dir = dist_path / "PxModRim.app"
    entry_app = dist_path / "entrypoint.app"
    if not app_dir.exists() and entry_app.exists():
        entry_app.rename(app_dir)

    if not app_dir.exists():
        print("Warning: PxModRim.app not found, skipping macOS bundle packaging")
        return None
    macos_dir = app_dir / "Contents" / "MacOS"
    if not macos_dir.exists():
        raise FileNotFoundError(f"Missing Contents/MacOS directory in {app_dir}")

    final_binary = normalize_executable(macos_dir, system="Darwin")

    plist_file = app_dir / "Contents" / "Info.plist"
    if plist_file.exists():
        import plistlib

        with open(plist_file, "rb") as f:
            pl = plistlib.load(f)
        pl["CFBundleIdentifier"] = "com.github.PyXiion.PxModRim"
        pl["CFBundleName"] = "PxModRim"
        pl["CFBundleDisplayName"] = "PxModRim"
        pl["CFBundleVersion"] = version
        pl["CFBundleShortVersionString"] = version
        pl["CFBundleExecutable"] = final_binary.name
        pl["CFBundlePackageType"] = "APPL"
        pl["NSHighResolutionCapable"] = True
        pl["LSMinimumSystemVersion"] = "11.0"
        with open(plist_file, "wb") as f:
            plistlib.dump(pl, f)
        print(f"Updated Info.plist bundle metadata (executable={final_binary.name})")

    zip_output = dist_path / "PxModRim-macOS"
    shutil.make_archive(
        str(zip_output), "zip", root_dir=str(dist_path), base_dir="PxModRim.app"
    )
    print(f"Created {zip_output}.zip")

    hdiutil = shutil.which("hdiutil")
    if hdiutil:
        dmg_output = dist_path / "PxModRim-macOS.dmg"
        if dmg_output.exists():
            dmg_output.unlink()
        subprocess.run(
            [
                hdiutil,
                "create",
                "-volname",
                "PxModRim",
                "-srcfolder",
                str(app_dir),
                "-ov",
                "-format",
                "UDZO",
                str(dmg_output),
            ],
            check=True,
        )
        print(f"Created {dmg_output}")

    return app_dir


def strip_unused_qml_modules(dist_dir: Path) -> None:
    """Remove QML modules not imported anywhere in the app's .qml files.

    The app only uses QtQuick, QtQuick.Controls, QtQuick.Layouts,
    QtWebChannel, and QtWebEngine.
    """
    qml_root = dist_dir / "PySide6" / "qml"
    if not qml_root.is_dir():
        return
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
        print(
            f"  Removed qtwebengine_devtools_resources.pak ({sz // (1024 * 1024)} MB)"
        )


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
    args = get_standalone_args(release=release)
    print(f"Running: {' '.join(args)}")

    result = subprocess.run(args, check=False)
    if result.returncode != 0:
        sys.exit(result.returncode)

    dist_dir = project_root / "dist" / "entrypoint.dist"
    if system == "Darwin":
        for candidate in [
            project_root / "dist" / "PxModRim.app" / "Contents" / "MacOS",
            project_root / "dist" / "entrypoint.app" / "Contents" / "MacOS",
        ]:
            if candidate.exists():
                dist_dir = candidate
                break

    print("\nStep 2a: Stripping non-English WebEngine locales...")
    strip_qt_locales(dist_dir)

    print("\nStep 2b: Stripping unused QML modules...")
    strip_unused_qml_modules(dist_dir)

    print("\nStep 2c: Stripping Qt translations...")
    strip_qt_translations(dist_dir)

    print("\nStep 2d: Stripping QtWebEngine DevTools...")
    strip_qt_devtools(dist_dir)

    if bundle_qt and system == "Linux":
        print("\nStep 2e: Copying missing Qt libraries...")
        copy_missing_libs(project_root)
    elif not bundle_qt and system == "Linux":
        print("\nStep 2e: Stripping bundled Qt libs (using system Qt)...")
        strip_bundled_qt(dist_dir)

    final_binary = normalize_executable(dist_dir, system=system)
    if system == "Linux":
        if bundle_qt:
            print("\nStep 3: Creating Linux packages (AppImage, deb, rpm)...")
            create_appimage(project_root)
            create_deb(project_root)
            create_rpm(project_root)
        else:
            print(
                "\nStep 3: Skipping Linux packages (system Qt mode, not self-contained)"
            )
    elif system == "Windows":
        print("\nStep 3: Creating Windows NSIS installer...")
        create_nsis_installer(project_root)
    elif system == "Darwin":
        print("\nStep 3: Packaging macOS .app bundle...")
        create_macos_bundle(project_root)

    print(f"\nBuild complete! Output: {dist_dir}")
    print(f"Run with: {final_binary}")

    size = _get_dir_size(dist_dir)
    print(f"Build size: {size // (1024 * 1024)} MB ({size:,} bytes)")


if __name__ == "__main__":
    main()
