from __future__ import annotations

import os
import sys
from pathlib import Path

import msgspec
from loguru import logger

from pxmodrim.core.constants import RIMWORLD_STEAM_APP_ID
from pxmodrim.core.msgspec_hooks import dec_hook, enc_hook
from pxmodrim.core.sort.config import SortSettings, TierConfig


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
    except Exception:
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


class ConfigService:
    __slots__ = ("config", "_path")

    def __init__(self, cfg: AppConfig, path: Path) -> None:
        self.config = cfg
        self._path = path

    def save(self) -> None:
        save_config(self.config, self._path)

    @property
    def path(self) -> Path:
        return self._path


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
    if not path.exists():
        return AppConfig()
    try:
        raw = path.read_bytes()
        return msgspec.json.decode(raw, type=AppConfig, dec_hook=dec_hook)
    except Exception as e:
        logger.warning(f"Failed to load config from {path}: {e}")
        return AppConfig()


def save_config(cfg: AppConfig, path: Path | None = None) -> None:
    """Serialize and write the core config (paths + sort) to a JSON file."""
    path = path or config_file_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    formatted = msgspec.json.format(
        msgspec.json.encode(cfg, enc_hook=enc_hook), indent=2
    )
    path.write_bytes(formatted)
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

    except Exception:
        pass
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
        except Exception:
            continue
    fallback = Path("C:/Program Files (x86)/Steam")
    game = fallback / "steamapps" / "common" / "RimWorld"
    if game.is_dir():
        result.game = str(game)
        result.local = str(game / "Mods")
        result.workshop = str(
            fallback / "steamapps" / "workshop" / "content" / steam_id
        )
    return result
