from __future__ import annotations

from typing import TYPE_CHECKING

from pxmodrim.core.models.metadata.structures import ListedMod
from pxmodrim.core.structures import CollectionStats

if TYPE_CHECKING:
    from pxmodrim.core.config import AppConfig

# Minimum valid version string shape: "X.Y" — reject anything shorter
_VERSION_MIN_PARTS = 2


class CoreContext:
    """Centralised application state for mods, config, and game version."""

    def __init__(self, cfg: AppConfig) -> None:
        self._cfg = cfg
        self._mods: dict[str, ListedMod] = {}
        self._active_uuids: list[str] = []
        self._game_version: str = "Unknown"
        self._refresh_game_version()

    def load(self, mods: dict[str, ListedMod], active_uuids: list[str]) -> None:
        """Replace all mods and active UUIDs, taking ownership of the data."""
        self._mods = dict(mods)
        self._active_uuids = list(active_uuids)

    def update_config(self, cfg: AppConfig) -> None:
        """Replace the live config and refresh derived values (game version)."""
        self._cfg = cfg
        self._refresh_game_version()

    # ── Game version ──────────────────────────────────────────────────────────

    def _refresh_game_version(self) -> None:
        """Read the game version from disk and cache it in `_game_version`."""
        from pxmodrim.core.config import read_game_version

        game = self._cfg.paths.game
        if not game:
            self._game_version = "Unknown"
            return
        version = read_game_version(game)
        self._game_version = version if version else "Unknown"

    @property
    def game_version(self) -> str:
        return self._game_version

    @property
    def target_version(self) -> str:
        parts = self._game_version.split(".")
        if len(parts) >= _VERSION_MIN_PARTS:
            return f"{parts[0]}.{parts[1]}"
        return "Unknown"

    # ── Mods ──────────────────────────────────────────────────────────────────

    @property
    def all_mods(self) -> dict[str, ListedMod]:
        return dict(self._mods)

    @property
    def active_uuids(self) -> list[str]:
        return list(self._active_uuids)

    @property
    def config(self) -> AppConfig:
        return self._cfg

    def compute_stats(self, active_ids: list[str] | None = None) -> CollectionStats:
        """Return total/active/inactive/error counts for the current mod set."""
        ids = active_ids if active_ids is not None else self._active_uuids
        stats = CollectionStats()
        stats.total = len(self._mods)
        stats.active = len(ids)
        stats.inactive = stats.total - stats.active
        for m in self._mods.values():
            if not m.valid:
                stats.errors += 1
        return stats
