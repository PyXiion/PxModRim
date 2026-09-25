from __future__ import annotations

import os
import shutil
import sys
from collections.abc import Callable
from pathlib import Path
from time import time
from typing import Any, TypeVar

import msgspec
from loguru import logger

from pxmodrim.core.constants import RIMWORLD_STEAM_APP_ID
from pxmodrim.core.msgspec_hooks import dec_hook, enc_hook
from pxmodrim.core.sort.config import SortSettings, TierConfig

StructT = TypeVar("StructT", bound=msgspec.Struct)
_JSON_SCHEMA_MARKER = "schema_version"
CURRENT_CONFIG_SCHEMA_VERSION = 1
_VERSIONED_CONFIG_FILES = frozenset({"config.json", "ui_prefs.json"})
_JsonMigration = Callable[[dict[str, Any]], None]


def _v1_json_migration(data: dict[str, Any]) -> None:
    return None


_JSON_MIGRATIONS: dict[int, _JsonMigration] = {1: _v1_json_migration}


def read_game_version(game_path: str | Path) -> str | None:
    """Read game version string from ``Version.txt`` in the game directory.

    Returns the stripped content (e.g. ``"1.6.4871 rev598"``) or ``None`` if
    the file is missing or cannot be read.
    """
    path = Path(game_path) / "Version.txt"
    if not path.exists():
        return None
    try:
        return path.read_text(encoding="utf-8").strip()
    except OSError:
        logger.warning("Failed to read game version from {}", path)
        return None


class PathConfig(msgspec.Struct):
    """Paths to the game, mods, workshop, and config directories."""

    game: str = ""
    local: str = ""
    workshop: str = ""
    config_folder: str = ""
    community_rules_file: str = ""
    no_version_warning_file: str = ""
    use_this_instead_file: str = ""
    steamcmd_prefix: str = ""


class AppConfig(msgspec.Struct):
    """Top-level core config: RimWorld paths and sort settings."""

    paths: PathConfig = msgspec.field(default_factory=PathConfig)
    sort: SortSettings = msgspec.field(
        default_factory=lambda: SortSettings(tier_config=TierConfig.default())
    )
    max_snapshots: int = 10


def _migrate_json(data: dict[str, Any], current: int) -> None:
    if current > CURRENT_CONFIG_SCHEMA_VERSION:
        raise ValueError(
            f"Unsupported JSON schema version {current}; "
            f"latest is {CURRENT_CONFIG_SCHEMA_VERSION}"
        )
    for target in range(current + 1, CURRENT_CONFIG_SCHEMA_VERSION + 1):
        migration = _JSON_MIGRATIONS.get(target)
        if migration is None:
            raise KeyError(f"Missing JSON migration step for target version {target}")
        migration(data)


class ConfigService:
    """Generic file-based config repository backed by a config directory.

    Provides typed load/save for msgspec structs. Core and UI layer
    wrappers live in their respective modules and use this service
    instead of calling ``config_dir()`` directly.
    """

    __slots__ = ("_config_dir",)

    def __init__(self, config_dir: Path) -> None:
        self._config_dir = config_dir

    @property
    def config_dir(self) -> Path:
        return self._config_dir

    def load(self, filename: str, struct_type: type[StructT]) -> StructT:
        path = self._config_dir / filename
        if not path.exists():
            return struct_type()
        try:
            if filename not in _VERSIONED_CONFIG_FILES:
                return msgspec.json.decode(
                    path.read_bytes(), type=struct_type, dec_hook=dec_hook
                )

            raw = msgspec.json.decode(path.read_bytes(), type=dict[str, Any])
            marker = raw.pop(_JSON_SCHEMA_MARKER, None)
            if marker is None:
                version = 0
            elif type(marker) is int and marker >= 0:
                version = marker
            else:
                raise msgspec.DecodeError("Invalid JSON schema version")

            if version < CURRENT_CONFIG_SCHEMA_VERSION:
                _migrate_json(raw, version)
                backup = path.with_suffix(f"{path.suffix}.bak.{int(time())}")
                shutil.copy2(path, backup)
                raw[_JSON_SCHEMA_MARKER] = CURRENT_CONFIG_SCHEMA_VERSION
                self._write(path, raw)
            elif version > CURRENT_CONFIG_SCHEMA_VERSION:
                _migrate_json(raw, version)

            return msgspec.json.decode(
                msgspec.json.encode(raw), type=struct_type, dec_hook=dec_hook
            )
        except (OSError, msgspec.DecodeError) as e:
            logger.warning(f"Failed to load {filename}: {e}")
            return struct_type()

    def save(self, filename: str, data: msgspec.Struct) -> None:
        path = self._config_dir / filename
        path.parent.mkdir(parents=True, exist_ok=True)
        encoded = msgspec.json.encode(data, enc_hook=enc_hook)
        if filename in _VERSIONED_CONFIG_FILES:
            raw = msgspec.json.decode(encoded, type=dict[str, Any])
            raw[_JSON_SCHEMA_MARKER] = CURRENT_CONFIG_SCHEMA_VERSION
            encoded = msgspec.json.encode(raw)
        self._write(path, encoded)

    @staticmethod
    def _write(path: Path, data: bytes | dict[str, Any]) -> None:
        encoded = msgspec.json.encode(data) if isinstance(data, dict) else data
        formatted = msgspec.json.format(encoded, indent=2)
        tmp = path.with_suffix(path.suffix + ".tmp")
        tmp.write_bytes(formatted)
        os.replace(tmp, path)


def config_dir() -> Path:
    base = os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")
    return Path(base) / "pxmodrim"


def config_file_path() -> Path:
    return config_dir() / "config.json"


def community_rules_file() -> Path:
    return config_dir() / "communityRules.json"


def load_config(path: Path | None = None) -> AppConfig:
    """Load config from a JSON file. Returns defaults if missing or corrupt."""
    path = path or config_file_path()
    return ConfigService(path.parent).load(path.name, AppConfig)


def save_config(cfg: AppConfig, path: Path | None = None) -> None:
    """Serialize and write the core config (paths + sort) to a JSON file."""
    path = path or config_file_path()
    ConfigService(path.parent).save(path.name, cfg)
    logger.info(f"Config saved to {path}")


def _detect_config_folder(steam_root: Path | None) -> str:
    """Locate the RimWorld Config folder for the current platform."""
    if sys.platform == "linux":
        if steam_root:
            proton = (
                steam_root
                / "steamapps"
                / "compatdata"
                / RIMWORLD_STEAM_APP_ID
                / "pfx"
                / "drive_c"
                / "users"
                / "steamuser"
                / "AppData"
                / "LocalLow"
                / "Ludeon Studios"
                / "RimWorld by Ludeon Studios"
                / "Config"
            )
            if proton.is_dir():
                logger.debug(f"Detected Proton config folder: {proton}")
                return str(proton)
        native = (
            Path.home()
            / ".config"
            / "unity3d"
            / "Ludeon Studios"
            / "RimWorld by Ludeon Studios"
            / "Config"
        )
        if native.is_dir():
            logger.debug(f"Detected native config folder: {native}")
            return str(native)
    elif sys.platform == "darwin":
        config = Path.home() / "Library" / "Application Support" / "Rimworld" / "Config"
        if config.is_dir():
            logger.debug(f"Detected macOS config folder: {config}")
            return str(config)
    elif sys.platform == "win32":
        config = (
            Path.home()
            / "AppData"
            / "LocalLow"
            / "Ludeon Studios"
            / "RimWorld by Ludeon Studios"
            / "Config"
        )
        if config.is_dir():
            logger.debug(f"Detected Windows config folder: {config}")
            return str(config)
    return ""


def detect_game_paths() -> PathConfig:
    """Auto-detect game, local mods, and workshop paths via Steam library folders."""
    result = PathConfig()
    steam_id = RIMWORLD_STEAM_APP_ID
    found_root: Path | None = None

    if sys.platform == "linux":
        candidates = [
            Path.home() / ".steam" / "debian-installation",
            Path.home() / ".steam" / "steam",
            Path.home() / ".local" / "share" / "Steam",
            Path.home()
            / ".var"
            / "app"
            / "com.valvesoftware.Steam"
            / ".local"
            / "share"
            / "Steam",
            Path.home() / "snap" / "steam" / "common" / ".local" / "share" / "Steam",
        ]
        for root in candidates:
            if not root.is_dir():
                continue
            game = _find_rimworld_in_steam_root(root, steam_id)
            if game:
                result.game = str(game)
                result.local = str(game / "Mods")
                result.workshop = str(_workshop_path_from_game(game, root, steam_id))
                found_root = root
                break
        if not result.game:
            fallback_root = Path.home() / ".steam" / "steam"
            fallback = fallback_root / "steamapps" / "common" / "RimWorld"
            if fallback.is_dir():
                result.game = str(fallback)
                result.local = str(fallback / "Mods")
                result.workshop = str(
                    fallback_root / "steamapps" / "workshop" / "content" / steam_id
                )
                found_root = fallback_root if fallback_root.is_dir() else None

    elif sys.platform == "darwin":
        steam_root = Path.home() / "Library" / "Application Support" / "Steam"
        found_root = steam_root if steam_root.is_dir() else None
        game = _find_rimworld_in_steam_root(steam_root, steam_id)
        if game:
            result.game = str(game)
            result.local = str(game / "Mods")
            result.workshop = str(_workshop_path_from_game(game, steam_root, steam_id))
        else:
            fallback = steam_root / "steamapps" / "common" / "RimWorld"
            app_bundle = _find_mac_app(fallback)
            if app_bundle:
                result.game = str(app_bundle)
                result.local = str(fallback / "Mods")
                result.workshop = str(
                    steam_root / "steamapps" / "workshop" / "content" / steam_id
                )

    elif sys.platform == "win32":
        result = _detect_windows_paths(steam_id)

    result.config_folder = _detect_config_folder(found_root)
    result.community_rules_file = str(community_rules_file())

    return result


def _find_rimworld_in_steam_root(root: Path, steam_id: str) -> Path | None:
    """Search a Steam library root for an installed RimWorld directory."""
    game = root / "steamapps" / "common" / "RimWorld"
    if game.is_dir():
        return game
    vdf_path = root / "config" / "libraryfolders.vdf"
    if vdf_path.exists():
        return _vdf_find_rimworld(vdf_path, steam_id)
    vdf_path = root / "steamapps" / "libraryfolders.vdf"
    if vdf_path.exists():
        return _vdf_find_rimworld(vdf_path, steam_id)
    return None


def _vdf_find_rimworld(vdf_path: Path, steam_id: str) -> Path | None:
    """Parse a libraryfolders.vdf to find RimWorld in alternate Steam library paths."""
    try:
        import re

        text = vdf_path.read_text(encoding="utf-8", errors="replace")

        # Simple regex-based VDF parser for libraryfolders.vdf
        # Looking for: "libraryfolders" { "0" { "path" "..." "apps"
        # { "294100" { ... } } } }
        path_pattern = re.compile(r'"path"\s+"(.+?)"')
        app_pattern = re.compile(r'"' + re.escape(steam_id) + r'"\s*{')

        current_path = None

        for line in text.splitlines():
            stripped = line.strip()

            # Match library folder path
            path_match = path_pattern.search(stripped)
            if path_match:
                current_path = Path(path_match.group(1))
                continue

            # Match app id entry
            if app_pattern.search(stripped) and current_path:
                game = current_path / "steamapps" / "common" / "RimWorld"
                if game.is_dir():
                    return game

    except OSError:
        logger.warning("Failed to parse libraryfolders.vdf")
    return None


def _workshop_path_from_game(game: Path, steam_root: Path, steam_id: str) -> Path:
    try:
        idx = game.parts.index("common")
        base = Path(*game.parts[:idx])
        workshop = base / "workshop" / "content" / steam_id
        if workshop.is_dir():
            return workshop
    except ValueError:
        pass
    return steam_root / "steamapps" / "workshop" / "content" / steam_id


def _find_mac_app(rimworld_dir: Path) -> Path | None:
    if rimworld_dir.is_dir():
        apps = list(rimworld_dir.glob("*.app"))
        if apps:
            return apps[0]
    return None


def _detect_windows_paths(steam_id: str) -> PathConfig:
    """Detect RimWorld paths on Windows via the Windows registry."""
    result = PathConfig()
    import winreg

    for reg_key in [
        r"SOFTWARE\Wow6432Node\Valve\Steam",
        r"SOFTWARE\Valve\Steam",
    ]:
        try:
            with winreg.OpenKey(  # type: ignore[attr-defined]
                winreg.HKEY_LOCAL_MACHINE,  # type: ignore[attr-defined]
                reg_key,
            ) as key:
                steam_path = winreg.QueryValueEx(  # type: ignore[attr-defined]
                    key, "InstallPath"
                )[0]
                steam_root = Path(steam_path)
                game = _find_rimworld_in_steam_root(steam_root, steam_id)
                if game:
                    result.game = str(game)
                    result.local = str(game / "Mods")
                    result.workshop = str(
                        _workshop_path_from_game(game, steam_root, steam_id)
                    )
                    return result
        except OSError:
            logger.debug("Unable to inspect Windows Steam registry key {}", reg_key)
    fallback = Path("C:/Program Files (x86)/Steam")
    game = fallback / "steamapps" / "common" / "RimWorld"
    if game.is_dir():
        result.game = str(game)
        result.local = str(game / "Mods")
        result.workshop = str(
            fallback / "steamapps" / "workshop" / "content" / steam_id
        )
    return result


class AppConfigService:
    """Owns AppConfig lifecycle: load and save.

    Follows the Service protocol (setup).
    """

    __slots__ = ("_cfg", "_svc")

    def __init__(self, svc: ConfigService) -> None:
        self._svc = svc
        self._cfg: AppConfig | None = None

    async def setup(self) -> None:
        self._cfg = self._svc.load("config.json", AppConfig)

    def cfg(self) -> AppConfig:
        assert self._cfg is not None
        return self._cfg

    def save_cfg(self) -> None:
        if self._cfg is not None:
            self._svc.save("config.json", self._cfg)
