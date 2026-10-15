from __future__ import annotations

from pxmodrim.core.downloads.downloader import Downloader
from pxmodrim.core.downloads.manager import DownloadManager, download_manager
from pxmodrim.core.downloads.types import (
    DownloadItemStatus,
    DownloadItemTitle,
    DownloadProgress,
    DownloadResult,
)

__all__ = [
    "DownloadItemStatus",
    "DownloadItemTitle",
    "DownloadManager",
    "DownloadProgress",
    "DownloadResult",
    "Downloader",
    "download_manager",
]
