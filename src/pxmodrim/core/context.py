from __future__ import annotations

from typing import TYPE_CHECKING

from pxmodrim.core.models.metadata.structures import ListedMod
from pxmodrim.core.structures import CollectionStats

if TYPE_CHECKING:
    from pxmodrim.core.config import AppConfig, PathConfig
    from pxmodrim.core.mod_service import ModService
    from pxmodrim.core.providers.base import BaseModProvider
    from pxmodrim.core.services.diagnostics_service import DiagnosticsService
    from pxmodrim.core.services.game_launcher import GameLauncher
    from pxmodrim.core.services.sort_service import SortService

# Minimum valid version string shape: "X.Y" — reject anything shorter
_VERSION_MIN_PARTS = 2


def _require_not_none[T](value: T | None, name: str) -> T:
    if value is None:
        raise RuntimeError(f"{name} accessed before initialisation")
    return value


class CoreContext:
    """Centralised application state for mods, config, and game version."""

    __slots__ = (
        "_cfg",
        "_mods",
        "_active_uuids",
        "_game_version",
        "_mod_service",
        "_diagnostics_service",
        "_sort_service",
        "_game_launcher",
        "_providers",
    )

    def __init__(self, cfg: AppConfig) -> None:
        self._cfg = cfg
        self._mods: dict[str, ListedMod] = {}
        self._active_uuids: list[str] = []
        self._game_version: str = "Unknown"
        self._refresh_game_version()
        self._mod_service: ModService | None = None
        self._diagnostics_service: DiagnosticsService | None = None
        self._sort_service: SortService | None = None
        self._game_launcher: GameLauncher | None = None
        self._providers: list[BaseModProvider] | None = None

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

    # ── Factory ────────────────────────────────────────────────────────────────

    @classmethod
    def create(cls, cfg: AppConfig) -> CoreContext:
        """Create a fully-initialised context with all core services."""
        from pxmodrim.core.mod_service import ModService
        from pxmodrim.core.providers import create_providers
        from pxmodrim.core.services.diagnostics_service import DiagnosticsService
        from pxmodrim.core.services.game_launcher import GameLauncher
        from pxmodrim.core.services.sort_service import SortService

        ctx = cls(cfg)
        ctx._providers = create_providers(cfg.paths)
        ctx._diagnostics_service = DiagnosticsService(ctx)
        ctx._mod_service = ModService(ctx, ctx._providers)
        ctx._sort_service = SortService(ctx, ctx._diagnostics_service)
        ctx._game_launcher = GameLauncher(ctx)
        return ctx

    # ── Service accessors ──────────────────────────────────────────────────────

    @property
    def mod_service(self) -> ModService:
        return _require_not_none(self._mod_service, "mod_service")

    @property
    def diagnostics_service(self) -> DiagnosticsService:
        return _require_not_none(self._diagnostics_service, "diagnostics_service")

    @property
    def sort_service(self) -> SortService:
        return _require_not_none(self._sort_service, "sort_service")

    @property
    def game_launcher(self) -> GameLauncher:
        return _require_not_none(self._game_launcher, "game_launcher")

    def reset_providers(self, paths: PathConfig) -> None:
        """Replace providers when the config changes (e.g. after settings dialog)."""
        from pxmodrim.core.providers import create_providers

        svc = _require_not_none(self._mod_service, "mod_service")
        self._providers = create_providers(paths)
        svc.reset_providers(self._providers)
