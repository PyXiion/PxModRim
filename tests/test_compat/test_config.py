from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

import pxmodrim.core.config as config_module
from pxmodrim.core.config import (
    AppConfig,
    AppConfigService,
    ConfigService,
    PathConfig,
    read_game_version,
)
from pxmodrim.ui.config import UIPrefsService
from pxmodrim.ui.ui_prefs import UIPrefs


class TestReadGameVersion:
    def test_parses_file(self, tmp_path: Path) -> None:
        ver_file = tmp_path / "Version.txt"
        ver_file.write_text("1.6.4871 rev598\n", encoding="utf-8")
        result = read_game_version(tmp_path)
        assert result == "1.6.4871 rev598"

    def test_strips_whitespace(self, tmp_path: Path) -> None:
        ver_file = tmp_path / "Version.txt"
        ver_file.write_text("  1.6.4871 rev598  \n", encoding="utf-8")
        result = read_game_version(tmp_path)
        assert result == "1.6.4871 rev598"

    def test_missing_file_returns_none(self, tmp_path: Path) -> None:
        result = read_game_version(tmp_path)
        assert result is None

    @pytest.mark.skipif(
        sys.platform == "win32", reason="chmod has no effect on Windows"
    )
    def test_unreadable_file_returns_none(self, tmp_path: Path) -> None:
        ver_file = tmp_path / "Version.txt"
        ver_file.write_text("1.6.4871 rev598\n")
        ver_file.chmod(0o000)
        try:
            result = read_game_version(tmp_path)
            assert result is None
        finally:
            ver_file.chmod(0o644)


async def test_legacy_app_config_migrates_with_backup(tmp_path: Path) -> None:
    service = ConfigService(tmp_path)
    service.save(
        "config.json",
        AppConfig(paths=PathConfig(game="/legacy/game", local="/legacy/mods")),
    )
    path = tmp_path / "config.json"
    legacy = json.loads(path.read_text())
    legacy.pop("schema_version")
    path.write_text(json.dumps(legacy), encoding="utf-8")

    app_service = AppConfigService(service)
    await app_service.setup()

    assert app_service.cfg().paths.game == "/legacy/game"
    migrated = json.loads(path.read_text())
    assert migrated["schema_version"] == 1
    assert json.loads(next(tmp_path.glob("config.json.bak.*")).read_text()) == legacy


async def test_legacy_ui_prefs_migrates_with_backup(tmp_path: Path) -> None:
    service = ConfigService(tmp_path)
    service.save("ui_prefs.json", UIPrefs(desc_expanded=True, validate_downloads=True))
    path = tmp_path / "ui_prefs.json"
    legacy = json.loads(path.read_text())
    legacy.pop("schema_version")
    path.write_text(json.dumps(legacy), encoding="utf-8")

    prefs_service = UIPrefsService(service)
    await prefs_service.setup()

    assert prefs_service.prefs().desc_expanded is True
    assert prefs_service.prefs().validate_downloads is True
    assert json.loads(path.read_text())["schema_version"] == 1
    assert json.loads(next(tmp_path.glob("ui_prefs.json.bak.*")).read_text()) == legacy


async def test_fresh_services_return_defaults_without_creating_files(
    tmp_path: Path,
) -> None:
    service = ConfigService(tmp_path)
    app_service = AppConfigService(service)
    prefs_service = UIPrefsService(service)
    await app_service.setup()
    await prefs_service.setup()

    assert app_service.cfg() == AppConfig()
    assert prefs_service.prefs() == UIPrefs()
    assert not (tmp_path / "config.json").exists()
    assert not (tmp_path / "ui_prefs.json").exists()


def test_managed_saves_stamp_schema_version(tmp_path: Path) -> None:
    service = ConfigService(tmp_path)
    service.save("config.json", AppConfig())
    service.save("ui_prefs.json", UIPrefs())

    assert json.loads((tmp_path / "config.json").read_text())["schema_version"] == 1
    assert json.loads((tmp_path / "ui_prefs.json").read_text())["schema_version"] == 1


def test_current_config_load_does_not_rewrite(tmp_path: Path) -> None:
    path = tmp_path / "config.json"
    path.write_text(
        json.dumps({"schema_version": 1, "paths": {"game": "/current"}}),
        encoding="utf-8",
    )
    before = path.read_bytes()

    assert (
        ConfigService(tmp_path).load("config.json", AppConfig).paths.game == "/current"
    )
    assert path.read_bytes() == before
    assert not list(tmp_path.glob("config.json.bak.*"))


def test_current_ui_prefs_load_does_not_rewrite(tmp_path: Path) -> None:
    path = tmp_path / "ui_prefs.json"
    path.write_text(
        json.dumps({"schema_version": 1, "desc_expanded": True}),
        encoding="utf-8",
    )
    before = path.read_bytes()

    assert ConfigService(tmp_path).load("ui_prefs.json", UIPrefs).desc_expanded is True
    assert path.read_bytes() == before
    assert not list(tmp_path.glob("ui_prefs.json.bak.*"))


def test_corrupt_config_returns_safe_defaults(tmp_path: Path) -> None:
    (tmp_path / "config.json").write_text("{invalid", encoding="utf-8")

    assert ConfigService(tmp_path).load("config.json", AppConfig) == AppConfig()


def test_unreadable_config_returns_safe_defaults(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    path = tmp_path / "config.json"
    path.write_text("{}", encoding="utf-8")

    def unreadable(_: Path) -> bytes:
        raise OSError("permission denied")

    monkeypatch.setattr(Path, "read_bytes", unreadable)
    assert ConfigService(tmp_path).load("config.json", AppConfig) == AppConfig()


def test_missing_json_migration_step_fails_without_rewrite(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    service = ConfigService(tmp_path)
    service.save("config.json", AppConfig())
    path = tmp_path / "config.json"
    legacy = json.loads(path.read_text())
    legacy.pop("schema_version")
    path.write_text(json.dumps(legacy), encoding="utf-8")
    before = path.read_bytes()
    monkeypatch.delitem(config_module._JSON_MIGRATIONS, 1)

    with pytest.raises(KeyError, match="Missing JSON migration step"):
        service.load("config.json", AppConfig)

    assert path.read_bytes() == before
    assert not list(tmp_path.glob("config.json.bak.*"))


def test_unsupported_config_schema_fails_without_mutation(tmp_path: Path) -> None:
    path = tmp_path / "config.json"
    path.write_text(json.dumps({"schema_version": 2}), encoding="utf-8")
    before = path.read_bytes()

    with pytest.raises(ValueError, match="Unsupported JSON schema version"):
        ConfigService(tmp_path).load("config.json", AppConfig)

    assert path.read_bytes() == before
