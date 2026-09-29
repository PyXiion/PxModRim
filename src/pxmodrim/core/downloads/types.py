from __future__ import annotations

import msgspec


class DownloadProgress(msgspec.Struct):
    total: int
    completed: int
    bytes_done: int
    bytes_total: int


class DownloadItemStatus(msgspec.Struct):
    mod_id: str
    status: str  # "downloading" | "success" | "error"
    bytes_done: int = 0
    bytes_total: int = 0
    error: str = ""


class DownloadItemTitle(msgspec.Struct):
    mod_id: str
    title: str


class DownloadResult(msgspec.Struct):
    succeeded: list[str]
    failed: list[str]
    # Succeeded items that actually transferred data (others were already current).
    changed: list[str] = msgspec.field(default_factory=list)
