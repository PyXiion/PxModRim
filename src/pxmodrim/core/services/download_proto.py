from __future__ import annotations

from typing import Any, Protocol, runtime_checkable


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
