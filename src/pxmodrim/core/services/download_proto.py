from __future__ import annotations

from typing import TYPE_CHECKING, Any, Protocol, runtime_checkable

from pxmodrim.core.events import Event

if TYPE_CHECKING:
    from pxmodrim.core.services.steam_cmd_service import (
        SteamCmdItemStatus,
        SteamCmdProgress,
        SteamCmdResult,
    )


@runtime_checkable
class DownloadRunner(Protocol):
    status: Any
    progress: Any
    item_status: Any
    finished: Any

    def start(self) -> None: ...
    def wait(self, msecs: int = ...) -> bool: ...
    def cancel(self) -> None: ...
    def quit(self) -> None: ...


@runtime_checkable
class DownloadService(Protocol):
    status_message_changed: Event[str]
    download_progress: Event[SteamCmdProgress]
    download_item_status_changed: Event[SteamCmdItemStatus]
    download_finished: Event[SteamCmdResult]

    def is_installed(self) -> bool: ...
    async def ensure_installed(
        self, prefix: str | None = None, reinstall: bool = False
    ) -> bool: ...
    async def download_mods(
        self,
        publishedfileids: list[str],
        validate: bool,
        titles: dict[str, str] | None = None,
    ) -> None: ...
    def cancel(self) -> None: ...
