from __future__ import annotations

import asyncio
import getpass
import platform
import urllib.error
import urllib.request
from importlib.metadata import PackageNotFoundError, version
from pathlib import Path
from typing import TYPE_CHECKING, Any

from pxmodrim.core.config import config_dir

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext

MAX_LOG_BYTES = 2 * 1024 * 1024
DEFAULT_UPLOAD_ENDPOINT = "https://paste.rs/"


def get_app_version() -> str:
    """Return the application version from package metadata, with fallback."""
    try:
        return version("pxmodrim")
    except PackageNotFoundError:
        return "0.1.0"


def redact(
    text: str,
    home_dir: str | Path | None = None,
    username: str | None = None,
) -> str:
    """Replace user home directory path with ~ and OS username with <user>."""
    home_path = Path.home() if home_dir is None else Path(home_dir)

    home_str = str(home_path).rstrip("/\\")

    if username is None:
        try:
            username = getpass.getuser()
        except (OSError, KeyError):
            username = ""

    # Redact home path first (handling native and alternate slash formats)
    if home_str and home_str not in ("/", "\\"):
        text = text.replace(home_str, "~")
        alt_slash = home_str.replace("\\", "/")
        if alt_slash != home_str:
            text = text.replace(alt_slash, "~")
        back_slash = home_str.replace("/", "\\")
        if back_slash != home_str:
            text = text.replace(back_slash, "~")

    # Redact username everywhere
    if username:
        text = text.replace(username, "<user>")

    return text


def read_log_tail(log_path: Path | None = None, max_bytes: int = MAX_LOG_BYTES) -> str:
    """Read up to max_bytes from the end of the log file."""
    path = log_path or (config_dir() / "logs" / "pxmodrim.log")
    if not path.is_file():
        return "<no log file found>"

    try:
        size = path.stat().st_size
    except OSError as exc:
        return f"<unable to read log file: {exc}>"

    if size == 0:
        return "<empty log file>"

    try:
        with path.open("rb") as f:
            if size <= max_bytes:
                data = f.read()
                return data.decode("utf-8", errors="replace")

            f.seek(size - max_bytes)
            data = f.read(max_bytes)
            text = data.decode("utf-8", errors="replace")
            first_newline = text.find("\n")
            if first_newline != -1:
                text = text[first_newline + 1 :]
            mb = max_bytes // (1024 * 1024)
            return f"[... log truncated to last ~{mb} MB ...]\n" + text
    except OSError as exc:
        return f"<error reading log file: {exc}>"


def build_report(
    ctx: CoreContext | Any,
    log_path: Path | None = None,
    home_dir: str | Path | None = None,
    username: str | None = None,
    max_log_bytes: int = MAX_LOG_BYTES,
) -> str:
    """Build a plain-text system diagnostic and log report with redacted user info."""
    lines: list[str] = []

    # App & runtime diagnostics
    app_ver = get_app_version()
    py_ver = platform.python_version()

    try:
        from PySide6.QtCore import qVersion

        qt_ver = qVersion()
    except (ImportError, RuntimeError, AttributeError):
        qt_ver = "Unknown"

    try:
        pyside_ver = version("PySide6")
    except PackageNotFoundError:
        try:
            import PySide6

            pyside_ver = getattr(PySide6, "__version__", "Unknown")
        except (ImportError, AttributeError):
            pyside_ver = "Unknown"

    os_info = f"{platform.system()} {platform.release()}"
    arch = platform.machine()

    lines.append("=== PxModRim System Report ===")
    lines.append(f"App Version: {app_ver}")
    lines.append(f"Python Version: {py_ver}")
    lines.append(f"Qt Version: {qt_ver}")
    lines.append(f"PySide6 Version: {pyside_ver}")
    lines.append(f"OS: {os_info} ({arch})")
    lines.append("")

    # Game version and configured paths from CoreContext
    game_version = getattr(ctx, "game_version", "Unknown")
    cfg = getattr(ctx, "config", None)
    paths = getattr(cfg, "paths", None) if cfg is not None else None

    lines.append("=== RimWorld Configuration ===")
    lines.append(f"Game Version: {game_version}")
    lines.append(f"Game Path: {getattr(paths, 'game', '') or '<none>'}")
    lines.append(f"Config Folder: {getattr(paths, 'config_folder', '') or '<none>'}")
    lines.append(f"Local Mods Path: {getattr(paths, 'local', '') or '<none>'}")
    lines.append(f"Workshop Mods Path: {getattr(paths, 'workshop', '') or '<none>'}")
    lines.append("")

    # Mod collection statistics
    all_mods = getattr(ctx, "all_mods", {})
    active_uuids = getattr(ctx, "active_uuids", [])
    total_mods_count = len(all_mods) if all_mods else 0
    active_mods_count = len(active_uuids) if active_uuids else 0

    lines.append("=== Mod Collection ===")
    lines.append(f"Total Mods: {total_mods_count}")
    lines.append(f"Active Mods: {active_mods_count}")
    lines.append("")

    # Active mods in load order
    lines.append("=== Active Mods (Load Order) ===")
    if active_uuids:
        for i, uuid in enumerate(active_uuids, 1):
            mod = all_mods.get(uuid) if all_mods else None
            if mod is not None:
                mod_name = getattr(mod, "name", "Unknown Mod Name")
                pid = getattr(mod, "package_id", "Unknown")
            else:
                mod_name = "Unknown Mod"
                pid = uuid
            lines.append(f"{i:3d}. {mod_name} ({pid})")
    else:
        lines.append("  <no active mods>")
    lines.append("")

    # Application log tail
    lines.append("=== Application Log ===")
    lines.append(read_log_tail(log_path, max_bytes=max_log_bytes))

    full_text = "\n".join(lines)
    return redact(full_text, home_dir=home_dir, username=username)


class ReportUploadError(Exception):
    """Raised when log or diagnostic report upload fails."""


class PasteUploader:
    """Uploader for posting plain-text reports to a paste service."""

    def __init__(
        self, endpoint: str = DEFAULT_UPLOAD_ENDPOINT, timeout: float = 30.0
    ) -> None:
        self.endpoint = endpoint
        self.timeout = timeout

    def upload(self, text: str) -> str:
        """Synchronously POST text to the endpoint and return the resulting URL.

        Raises ReportUploadError on non-2xx response, network failure, or non-URL body.
        """
        if not text or not text.strip():
            raise ReportUploadError("Report text is empty")

        data = text.encode("utf-8")
        app_ver = get_app_version()
        headers = {
            "User-Agent": f"PxModRim/{app_ver}",
            "Content-Type": "text/plain; charset=utf-8",
        }
        req = urllib.request.Request(
            self.endpoint,
            data=data,
            headers=headers,
            method="POST",
        )

        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as response:
                status = getattr(response, "status", 200)
                if not (200 <= status < 300):
                    raise ReportUploadError(f"Upload failed: HTTP status {status}")
                body = response.read().decode("utf-8").strip()
        except urllib.error.HTTPError as exc:
            raise ReportUploadError(
                f"Upload failed: HTTP {exc.code} {exc.reason}"
            ) from exc
        except urllib.error.URLError as exc:
            raise ReportUploadError(
                f"Upload failed: network error ({exc.reason})"
            ) from exc
        except TimeoutError as exc:
            raise ReportUploadError(
                "Upload failed: request timed out after 30s"
            ) from exc
        except OSError as exc:
            raise ReportUploadError(f"Upload failed: {exc}") from exc

        if not body.startswith(("http://", "https://")):
            raise ReportUploadError(
                f"Upload failed: server did not return a valid URL: {body[:100]!r}"
            )

        return body

    async def async_upload(self, text: str) -> str:
        """Asynchronously upload text using asyncio.to_thread."""
        return await asyncio.to_thread(self.upload, text)
