from __future__ import annotations

import asyncio
from pathlib import Path

from loguru import logger
from ttimer import Timer

from pxmodrim.core.context import CoreContext
from pxmodrim.core.events import Event
from pxmodrim.core.models.metadata.structures import (
    AboutXmlMod,
    CaseInsensitiveStr,
    ListedMod,
    ModsConfig,
)
from pxmodrim.core.mods_config import (
    create_snapshot,
    get_snapshots,
    parse_mods_config,
    restore_snapshot,
    write_mods_config,
)
from pxmodrim.core.profiler import adopt
from pxmodrim.core.providers.base import BaseModProvider
from pxmodrim.core.services.metadata_cache import (
    MetadataCache,
    metadata_cache_path,
    normalize_path,
)
from pxmodrim.core.services.mod_discovery import resolve_active_uuids
from pxmodrim.core.services.startup_impact_service import StartupImpactService
from pxmodrim.core.structures import CollectionStats


class ModService:
    """Orchestrates mod discovery and lifecycle across a set of providers."""

    __slots__ = (
        "_ctx",
        "_metadata_cache",
        "_providers",
        "_startup_impact_service",
        "mods_changed",
    )

    def __init__(
        self,
        ctx: CoreContext,
        providers: list[BaseModProvider],
        metadata_cache: MetadataCache | None = None,
    ) -> None:
        self._ctx = ctx
        self._providers: dict[str, BaseModProvider] = {
            p.provider_id: p for p in providers
        }
        self._startup_impact_service = StartupImpactService(ctx)
        if metadata_cache is not None:
            self._metadata_cache = metadata_cache
        elif ctx.has_config_service:
            self._metadata_cache = MetadataCache(
                metadata_cache_path(ctx.config_service.config_dir)
            )
        else:
            self._metadata_cache = None
        self.mods_changed: Event[None] = Event()

    @property
    def startup_impact(self) -> StartupImpactService:
        return self._startup_impact_service

    def reset_providers(self, providers: list[BaseModProvider]) -> None:
        """Replace the current provider set with a new list."""
        self._providers = {p.provider_id: p for p in providers}

    def get_provider(self, provider_id: str) -> BaseModProvider | None:
        """Retrieve a provider by its ``provider_id``, or ``None``."""
        return self._providers.get(provider_id)

    @property
    def provider_colors(self) -> dict[str, str]:
        return {pid: p.color for pid, p in self._providers.items()}

    async def discover(self, timer: Timer | None = None) -> None:
        """Run all providers' discovery in parallel and load results into context."""
        own = timer is None
        if own:
            timer = Timer()
        with timer("discover"):
            async with asyncio.TaskGroup() as tg:
                provs = [
                    tg.create_task(self._discover_one(p))
                    for p in self._providers.values()
                ]
            all_mods: dict[str, ListedMod] = {}
            for coro in provs:
                mods, child = coro.result()
                adopt(timer, child)
                all_mods.update(mods)
            with timer("cache_prune"):
                if self._metadata_cache is not None:
                    live_paths = {
                        normalize_path(m.mod_path)
                        for m in all_mods.values()
                        if m.mod_path is not None
                    }
                    await self._metadata_cache.prune_disappeared(live_paths)
            with timer("resolve_active"):
                active = resolve_active_uuids(
                    all_mods, self._ctx.config.paths.config_folder
                )
                self._ctx.load(all_mods, active)
        if own:
            logger.debug("Profile report:\n{}", timer.render())

    async def _discover_one(
        self, p: BaseModProvider
    ) -> tuple[dict[str, ListedMod], Timer]:
        t = Timer()
        with t(f"provider.{p.provider_id}"):
            mods = await p.discover(
                self._ctx.target_version,
                timer=t,
                metadata_cache=self._metadata_cache,
            )
        return mods, t

    async def _timed(self, label: str, fn) -> Timer:
        """Run *fn(timer)* wrapped in *label*, return the populated Timer."""
        t = Timer()
        with t(label):
            await fn(t)
        return t

    async def _run_refresh_pipeline(self, label: str) -> None:
        """Discover mods, init diagnostics, and load startup impact, then rebuild
        diagnostics and notify listeners. Shared by :meth:`initialize` and
        :meth:`reload`."""
        timer = Timer()
        with timer(f"mod_service.{label}"):
            async with asyncio.TaskGroup() as tg:
                d = tg.create_task(
                    self._timed("discover", lambda t: self.discover(timer=t))
                )
                diag = tg.create_task(
                    self._timed(
                        "diagnostics",
                        lambda t: self._ctx.diagnostics_service.initialize(timer=t),
                    )
                )
                si = tg.create_task(
                    self._timed(
                        "startup_impact",
                        lambda _: self._startup_impact_service.load(),
                    )
                )
            adopt(timer, d.result())
            adopt(timer, diag.result())
            adopt(timer, si.result())
            self._ctx.diagnostics_service.rebuild(self._ctx.active_uuids, timer=timer)
        logger.debug("Profile report:\n{}", timer.render())
        self.mods_changed.emit(None)

    def _snapshot_current_config(self) -> Path | None:
        config_folder = self._ctx.config.paths.config_folder
        snapshots_dir = self._snapshots_dir()
        if not config_folder or snapshots_dir is None:
            return None
        config_path = Path(config_folder) / "ModsConfig.xml"
        if not config_path.is_file():
            return None
        return create_snapshot(
            config_path,
            snapshots_dir,
            max_snapshots=self._ctx.config.max_snapshots,
        )

    async def initialize(self) -> None:
        """One‑time startup initialisation: discover, init diagnostics, load impact."""
        self._snapshot_current_config()
        await self._run_refresh_pipeline("initialize")

    async def reload(self) -> None:
        """Full refresh triggered by the user. Same pipeline as initialize."""
        await self._run_refresh_pipeline("reload")

    def compute_stats(self, active_ids: list[str] | None = None) -> CollectionStats:
        return self._ctx.compute_stats(active_ids)

    def _snapshots_dir(self) -> Path | None:
        if not self._ctx.has_config_service:
            return None
        return self._ctx.config_service.config_dir / "snapshots"

    def get_snapshots(self) -> list[Path]:
        """Return available mod-list snapshots, sorted newest first."""
        snapshots_dir = self._snapshots_dir()
        return get_snapshots(snapshots_dir) if snapshots_dir is not None else []

    async def restore_snapshot(self, snapshot_path: Path) -> bool:
        """Restore a snapshot to ModsConfig.xml and reload live state."""
        config_folder = self._ctx.config.paths.config_folder
        snapshots_dir = self._snapshots_dir()
        if not config_folder or snapshots_dir is None:
            logger.warning("Cannot restore snapshot: configuration is incomplete")
            return False

        restored = restore_snapshot(
            snapshot_path,
            Path(config_folder) / "ModsConfig.xml",
            snapshots_dir,
            max_snapshots=self._ctx.config.max_snapshots,
        )
        if restored:
            await self.reload()
        return restored

    async def save_active_layout(self, active_ids: list[str]) -> bool:
        """Persist the given active UUIDs as a ModsConfig.xml file."""
        config_folder = self._ctx.config.paths.config_folder
        snapshots_dir = self._snapshots_dir()
        if not config_folder or snapshots_dir is None:
            return False

        mods = self._ctx.all_mods
        package_ids: list[CaseInsensitiveStr] = []
        for uuid in active_ids:
            mod = mods.get(uuid)
            if isinstance(mod, AboutXmlMod):
                package_ids.append(mod.package_id)
        config_path = Path(config_folder) / "ModsConfig.xml"
        existing = parse_mods_config(config_path)
        data = ModsConfig(
            version=self._ctx.target_version,
            activeMods=package_ids,
            knownExpansions=existing.knownExpansions if existing else [],
        )
        write_mods_config(
            config_path,
            data,
            snapshots_dir,
            max_snapshots=self._ctx.config.max_snapshots,
        )
        return True

    async def close(self) -> None:
        if self._metadata_cache is not None:
            await self._metadata_cache.close()

    def close_sync(self) -> None:
        if self._metadata_cache is not None:
            self._metadata_cache.close_sync()
