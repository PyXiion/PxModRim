from __future__ import annotations

from abc import ABC, abstractmethod
from collections.abc import Iterable
from typing import TYPE_CHECKING, ClassVar

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

MANAGER_NAME = "downloads"


class Downloader(Plugin, ABC):
    """A source that downloads and updates mods into the local mods folder.

    Registers itself with the ``downloads`` manager in ``setup()``; the UI only
    talks to the manager, so a downloader can be added or left out freely.
    """

    dependencies: ClassVar[list[str]] = [MANAGER_NAME]
    # Human-readable source name shown in the UI, e.g. "Steam Workshop".
    label: ClassVar[str] = ""

    status_message_changed: Event[str]
    download_progress: Event[DownloadProgress]
    download_item_status_changed: Event[DownloadItemStatus]
    download_item_titled: Event[DownloadItemTitle]
    # "login" -> "query" (item details) -> "run" (all items resolved)
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

    def setup(self, ctx: CoreContext) -> None:
        # Deferred: the manager module imports this one.
        from pxmodrim.core.downloads.manager import download_manager

        download_manager(ctx).register(self)

    @property
    @abstractmethod
    def is_downloading(self) -> bool: ...

    @property
    @abstractmethod
    def active_ids(self) -> frozenset[str]:
        """Ids of the batch currently downloading."""

    @abstractmethod
    def accepts(self, mod_id: str) -> bool:
        """Whether *mod_id* belongs to this source."""

    @abstractmethod
    def updatable_id(self, mod: ListedMod) -> str | None:
        """Id of *mod* in this source if PxModRim downloaded it from here."""

    @abstractmethod
    def last_synced(self, mod: ListedMod) -> float | None:
        """Unix time of the last successful sync of *mod*, if known."""

    @abstractmethod
    async def download_mods(self, mod_ids: list[str]) -> DownloadResult:
        """Download or update *mod_ids*; per-item failures don't raise."""

    @abstractmethod
    def cancel(self) -> None: ...

    def updatable_ids(self, mods: Iterable[ListedMod]) -> list[str]:
        ids = (self.updatable_id(m) for m in mods)
        return list(dict.fromkeys(pid for pid in ids if pid is not None))
