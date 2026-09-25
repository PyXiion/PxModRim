from __future__ import annotations

import configparser
import importlib.util
import plistlib
import struct
import xml.etree.ElementTree as ET
from pathlib import Path

import pytest

_build_py = Path(__file__).parent.parent / "packaging" / "build.py"
_spec = importlib.util.spec_from_file_location("local_packaging_build", _build_py)
assert _spec and _spec.loader
pkg_build = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pkg_build)


def test_get_version_from_pyproject(tmp_path: Path, monkeypatch) -> None:
    monkeypatch.delenv("GITHUB_REF_NAME", raising=False)
    pyproject = tmp_path / "pyproject.toml"
    pyproject.write_text('[project]\nversion = "1.2.3"\n', encoding="utf-8")
    assert pkg_build.get_version(tmp_path) == "1.2.3"


def test_get_version_from_git_tag(tmp_path: Path, monkeypatch) -> None:
    monkeypatch.setenv("GITHUB_REF_NAME", "v2.0.1")
    pyproject = tmp_path / "pyproject.toml"
    pyproject.write_text('[project]\nversion = "1.0.0"\n', encoding="utf-8")
    assert pkg_build.get_version(tmp_path) == "2.0.1"


def test_get_standalone_platform_args(monkeypatch) -> None:
    monkeypatch.setattr(pkg_build.platform, "system", lambda: "Darwin")
    args = pkg_build.get_standalone_args()
    assert "--macos-create-app-bundle" in args
    assert "--macos-app-name=PxModRim" in args
    assert "--macos-signed-app-name=com.github.PyXiion.PxModRim" in args

    monkeypatch.setattr(pkg_build.platform, "system", lambda: "Windows")
    args = pkg_build.get_standalone_args()
    assert "--windows-company-name=PxModRim" in args
    assert "--windows-product-name=PxModRim" in args


def test_strip_unused_qml_modules_leaves_missing_directory_unchanged(
    tmp_path: Path,
) -> None:
    sentinel = tmp_path / "keep.txt"
    sentinel.write_text("keep", encoding="utf-8")

    pkg_build.strip_unused_qml_modules(tmp_path)

    assert sentinel.read_text(encoding="utf-8") == "keep"


def test_windows_executable_normalization(tmp_path: Path) -> None:
    dist_dir = tmp_path / "entrypoint.dist"
    dist_dir.mkdir()
    source = dist_dir / "entrypoint.exe"
    source.write_bytes(b"windows executable")

    result = pkg_build.normalize_executable(dist_dir, system="Windows")

    assert result == dist_dir / "PxModRim.exe"
    assert result.read_bytes() == b"windows executable"
    assert not source.exists()

    with pytest.raises(FileNotFoundError, match="Executable not found"):
        pkg_build.normalize_executable(tmp_path / "missing", system="Windows")


def test_deb_creation_requires_dpkg_deb(tmp_path: Path, monkeypatch) -> None:
    dist_dir = tmp_path / "dist" / "entrypoint.dist"
    dist_dir.mkdir(parents=True)
    (tmp_path / "pyproject.toml").write_text(
        '[project]\nversion = "0.5.0"\n', encoding="utf-8"
    )
    monkeypatch.setattr(pkg_build.shutil, "which", lambda cmd: None)

    with pytest.raises(RuntimeError, match="dpkg-deb"):
        pkg_build.create_deb(tmp_path)


def test_flatpak_metadata_contract() -> None:
    metadata = ET.parse(
        "packaging/flatpak/com.github.PyXiion.PxModRim.metainfo.xml"
    ).getroot()
    assert metadata.findtext("id") == "com.github.PyXiion.PxModRim"
    assert metadata.findtext("project_license") == "LGPL-3.0"
    urls = {url.attrib["type"]: url.text for url in metadata.findall("url")}
    assert urls == {
        "homepage": "https://github.com/PyXiion/PxModRim",
        "bugtracker": "https://github.com/PyXiion/PxModRim/issues",
    }


def test_desktop_entry_contract() -> None:
    desktop = configparser.ConfigParser(interpolation=None)
    desktop.read("packaging/flatpak/com.github.PyXiion.PxModRim.desktop")
    entry = desktop["Desktop Entry"]
    assert entry["Icon"] == "com.github.PyXiion.PxModRim"
    assert entry["Exec"] == "PxModRim"


def test_logo_icns_header() -> None:
    icns = Path("packaging/logo.icns").read_bytes()
    assert icns[:4] == b"icns"
    total_len = struct.unpack(">I", icns[4:8])[0]
    assert total_len == len(icns)
    assert icns[8:12] == b"ic09"


def test_macos_bundle_plist_update(tmp_path: Path, monkeypatch) -> None:
    project_root = tmp_path
    pyproject = project_root / "pyproject.toml"
    pyproject.write_text('[project]\nversion = "0.9.0"\n', encoding="utf-8")

    app_dir = project_root / "dist" / "PxModRim.app"
    contents = app_dir / "Contents"
    macos_dir = contents / "MacOS"
    macos_dir.mkdir(parents=True)
    (macos_dir / "entrypoint").write_bytes(b"macOS executable")
    plist_file = contents / "Info.plist"

    with open(plist_file, "wb") as f:
        plistlib.dump({}, f)

    # Mock hdiutil not present to only test plist update and zip creation
    monkeypatch.setattr(pkg_build.shutil, "which", lambda cmd: None)

    res = pkg_build.create_macos_bundle(project_root)
    assert res == app_dir
    assert (macos_dir / "PxModRim").read_bytes() == b"macOS executable"
    assert not (macos_dir / "entrypoint").exists()

    with open(plist_file, "rb") as f:
        pl = plistlib.load(f)

    assert pl["CFBundleIdentifier"] == "com.github.PyXiion.PxModRim"
    assert pl["CFBundleName"] == "PxModRim"
    assert pl["CFBundleVersion"] == "0.9.0"
    assert pl["CFBundleExecutable"] == "PxModRim"
    assert pl["CFBundlePackageType"] == "APPL"
