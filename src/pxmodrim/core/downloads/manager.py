from __future__ import annotations

from collections.abc import Iterable
from typing import TYPE_CHECKING

from pxmodrim.core.downloads.downloader import MANAGER_NAME, Downloader
from pxmodrim.core.downloads.types import (
    DownloadItemStatus,
    DownloadItemTitle,
    DownloadProgress,
    DownloadResult,
)
from pxmodrim.core.events import Event
from pxmodrim.core.plugin import Plugin

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext
    from pxmodrim.core.models.metadata.structures import ListedMod


class DownloadManager(Plugin):
    """The one download entry point for the UI; routes to registered downloaders.

    With no downloader registered nothing is updatable and ``available`` is False,
    which is how the UI hides every download control.
    """

    name = MANAGER_NAME

    status_message_changed: Event[str]
    download_progress: Event[DownloadProgress]
    download_item_status_changed: Event[DownloadItemStatus]
    download_item_titled: Event[DownloadItemTitle]
    download_phase_changed: Event[str]
    download_finished: Event[DownloadResult]
    busy_changed: Event[bool]
    batch_started: Event[list[str]]

    def __init__(self) -> None:
        self.status_message_changed = Event()
        self.download_progress = Event()
        self.download_item_status_changed = Event()
        self.download_item_titled = Event()
        self.download_phase_changed = Event()
        self.download_finished = Event()
        self.busy_changed = Event()
        self.batch_started = Event()
        self._downloaders: list[Downloader] = []
        self._cancelled = False

    def register(self, downloader: Downloader) -> None:
        self._downloaders.append(downloader)
        downloader.status_message_changed.connect(self.status_message_changed.emit)
        downloader.download_progress.connect(self.download_progress.emit)
        downloader.download_item_status_changed.connect(
            self.download_item_status_changed.emit
        )
        downloader.download_item_titled.connect(self.download_item_titled.emit)
        downloader.download_phase_changed.connect(self.download_phase_changed.emit)
        downloader.download_finished.connect(self.download_finished.emit)
        downloader.batch_started.connect(self.batch_started.emit)
        downloader.busy_changed.connect(self._on_busy)

    def _on_busy(self, busy: bool) -> None:
        if busy:
            self._cancelled = False
        self.busy_changed.emit(self.is_downloading)

    @property
    def downloaders(self) -> tuple[Downloader, ...]:
        return tuple(self._downloaders)

    @property
    def available(self) -> bool:
        return bool(self._downloaders)

    @property
    def is_downloading(self) -> bool:
        return any(d.is_downloading for d in self._downloaders)

    @property
    def active_ids(self) -> frozenset[str]:
        return frozenset().union(*(d.active_ids for d in self._downloaders))

    def source_of(self, mod: ListedMod) -> Downloader | None:
        """The downloader that can update *mod*, if any."""
        return next((d for d in self._downloaders if d.updatable_id(mod)), None)

    def updatable_id(self, mod: ListedMod) -> str | None:
        source = self.source_of(mod)
        return None if source is None else source.updatable_id(mod)

    def updatable_ids(self, mods: Iterable[ListedMod]) -> list[str]:
        mods = list(mods)
        ids: dict[str, None] = {}
        for downloader in self._downloaders:
            ids.update(dict.fromkeys(downloader.updatable_ids(mods)))
        return list(ids)

    def names_by_id(self, mods: Iterable[ListedMod]) -> dict[str, str]:
        """Display names of installed *mods*, keyed by their source id."""
        return {
            mod_id: mod.name
            for mod in mods
            if (mod_id := self.updatable_id(mod)) is not None
        }

    def last_synced(self, mod: ListedMod) -> float | None:
        source = self.source_of(mod)
        return None if source is None else source.last_synced(mod)

    async def download_mods(self, mod_ids: list[str]) -> DownloadResult:
        """Download *mod_ids*, one downloader after another (single-flight overall)."""
        groups: dict[Downloader, list[str]] = {}
        for mod_id in dict.fromkeys(mod_ids):
            owner = next((d for d in self._downloaders if d.accepts(mod_id)), None)
            if owner is None:
                raise ValueError(f"No download source handles '{mod_id}'.")
            groups.setdefault(owner, []).append(mod_id)
        if not groups:
            raise ValueError("No mods selected for download.")
        if self.is_downloading:
            raise RuntimeError("A download is already running.")

        self._cancelled = False
        total = DownloadResult(succeeded=[], failed=[])
        for downloader, group in groups.items():
            if self._cancelled:
                break
            result = await downloader.download_mods(group)
            total.succeeded += result.succeeded
            total.failed += result.failed
            total.changed += result.changed
        return total

    @property
    def cancelled(self) -> bool:
        return self._cancelled

    def cancel(self) -> None:
        self._cancelled = True
        for downloader in self._downloaders:
            if downloader.is_downloading:
                downloader.cancel()


def download_manager(ctx: CoreContext) -> DownloadManager:
    manager = ctx.plugins.get(MANAGER_NAME)
    if isinstance(manager, DownloadManager):
        return manager
    raise RuntimeError(f"'{MANAGER_NAME}' plugin is not registered")
