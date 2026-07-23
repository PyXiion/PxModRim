from __future__ import annotations

import asyncio
import os
import platform
import shutil
import sys
import tarfile
import tempfile
import zipfile
from collections.abc import Callable
from io import BytesIO
from pathlib import Path
from typing import TYPE_CHECKING

import httpx
import msgspec
from loguru import logger

from pxmodrim.core.config import ConfigService, config_dir
from pxmodrim.core.constants import RIMWORLD_STEAM_APP_ID
from pxmodrim.core.context import CoreContext
from pxmodrim.core.events import Event

if TYPE_CHECKING:
    from pxmodrim.core.loading import LoadingState
    from pxmodrim.core.services.download_proto import DownloadRunner

STEAMCMD_BATCH_SIZE = 25


class SymlinkConflictError(ValueError):
    """
    Raised when ``ensure_symlink`` finds an existing file or directory.
    """


def _remove_symlink_conflict(dst: Path, forced: bool) -> None:
    if dst.is_symlink() or (sys.platform == "win32" and os.path.isjunction(dst)):
        logger.info("[steamcmd] removing symlink conflict: {}", dst)
        dst.unlink()
    elif dst.is_dir() or dst.exists():
        if not forced:
            raise SymlinkConflictError(
                f"A real {'directory' if dst.is_dir() else 'file'} "
                f"exists at the symlink target ({dst}). "
                "Use forced=True to overwrite."
            )
        if dst.is_dir():
            shutil.rmtree(dst)
        else:
            dst.unlink()


class SteamCmdProgress(msgspec.Struct):
    total: int
    completed: int
    current_id: str = ""
    current_title: str = ""


class SteamCmdItemStatus(msgspec.Struct):
    mod_id: str
    status: str  # "downloading" | "success" | "error"


class SteamCmdResult(msgspec.Struct):
    succeeded: list[str]
    failed: list[str]


_STEAMCMD_URLS = {
    "Darwin": "https://steamcdn-a.akamaihd.net/client/installer/steamcmd_osx.tar.gz",
    "Linux": "https://steamcdn-a.akamaihd.net/client/installer/steamcmd_linux.tar.gz",
    "Windows": "https://steamcdn-a.akamaihd.net/client/installer/steamcmd.zip",
}


async def _download_bytes(url: str) -> bytes:
    async with httpx.AsyncClient(timeout=30.0) as client:
        resp = await client.get(url, follow_redirects=True)
        resp.raise_for_status()
        return resp.content


def _is_safe_member(member: tarfile.TarInfo | zipfile.ZipInfo) -> bool:
    name = member.name if isinstance(member, tarfile.TarInfo) else member.filename
    return not os.path.isabs(name) and ".." not in Path(name).parts


def _safe_members(archive: zipfile.ZipFile | tarfile.TarFile) -> list:
    """Return only safe archive members, raising if any are unsafe."""
    if isinstance(archive, zipfile.ZipFile):
        members = archive.infolist()
        name_attr = "filename"
        kind = "zip"
    else:
        members = archive.getmembers()
        name_attr = "name"
        kind = "tar"
    unsafe = [m for m in members if not _is_safe_member(m)]
    if unsafe:
        for m in unsafe:
            logger.warning(
                "[steamcmd] rejecting unsafe archive member: {}",
                getattr(m, name_attr),
            )
        raise ValueError(
            f"Unsafe {kind} entries detected: "
            f"{[getattr(m, name_attr) for m in unsafe]}"
        )
    return members


def _extract_archive(data: bytes, url: str, dest: str) -> None:
    os.makedirs(dest, exist_ok=True)
    if ".zip" in url:
        with zipfile.ZipFile(BytesIO(data)) as archive:
            archive.extractall(dest, members=_safe_members(archive))
    elif ".tar.gz" in url:
        with tarfile.open(fileobj=BytesIO(data), mode="r:gz") as archive:
            archive.extractall(dest, members=_safe_members(archive))
    else:
        raise ValueError(f"Unsupported SteamCMD archive URL: {url}")


class SteamCmdService:
    status_message_changed: Event[str]
    download_progress: Event[SteamCmdProgress]
    download_item_status_changed: Event[SteamCmdItemStatus]
    download_finished: Event[SteamCmdResult]

    __slots__ = (
        "status_message_changed",
        "download_progress",
        "download_item_status_changed",
        "download_finished",
        "_ctx",
        "_config",
        "_prefix",
        "_install_path",
        "_steam_path",
        "_content_path",
        "_executable",
        "_worker",
        "_runner_factory",
    )

    def __init__(
        self,
        ctx: CoreContext,
        config_service: ConfigService,
        runner_factory: Callable[..., DownloadRunner] | None = None,
    ) -> None:
        """Initialize the SteamCMD service with core context and config."""
        self.status_message_changed = Event()
        self.download_progress = Event()
        self.download_item_status_changed = Event()
        self.download_finished = Event()

        self._ctx = ctx
        self._config = config_service
        prefix = ctx.config.paths.steamcmd_prefix or str(config_dir() / "steamcmd")
        self._derive_paths(prefix)
        self._worker = None
        self._runner_factory = runner_factory

    # ── Path derivation ───────────────────────────────────────────────────────

    def _derive_paths(self, prefix: str) -> None:
        self._prefix = prefix
        self._install_path = str(Path(prefix) / "steamcmd")
        self._steam_path = str(Path(prefix) / "steam")
        self._content_path = str(
            Path(self._steam_path) / "steamapps" / "workshop" / "content"
        )
        exe_name = "steamcmd.exe" if sys.platform == "win32" else "steamcmd.sh"
        self._executable = str(Path(self._install_path) / exe_name)

    # ── Properties ─────────────────────────────────────────────────────────────

    @property
    def prefix(self) -> str:
        return self._prefix

    @property
    def install_path(self) -> str:
        return self._install_path

    @property
    def steam_path(self) -> str:
        return self._steam_path

    @property
    def content_path(self) -> str:
        return self._content_path

    @property
    def executable(self) -> str:
        return self._executable

    @property
    def symlink_target(self) -> str:
        return str(Path(self._content_path) / RIMWORLD_STEAM_APP_ID)

    # ── State ──────────────────────────────────────────────────────────────────

    def is_installed(self) -> bool:
        return os.path.exists(self._executable)

    def set_prefix(self, prefix: str) -> None:
        if not prefix:
            prefix = str(config_dir() / "steamcmd")
        self._derive_paths(prefix)

    # ── Batch helpers ─────────────────────────────────────────────────────────

    @staticmethod
    def split_batches(publishedfileids: list[str]) -> list[list[str]]:
        total = len(publishedfileids)
        return [
            publishedfileids[i : i + STEAMCMD_BATCH_SIZE]
            for i in range(0, total, STEAMCMD_BATCH_SIZE)
        ]

    def _build_download_script(
        self, publishedfileids: list[str], validate: bool = False
    ) -> str:
        download_cmd = f"workshop_download_item {RIMWORLD_STEAM_APP_ID}"
        script_lines = [
            f'force_install_dir "{self._steam_path}"',
            "login anonymous",
        ]
        for pfid in publishedfileids:
            if validate:
                script_lines.append(f"{download_cmd} {pfid} validate")
            else:
                script_lines.append(f"{download_cmd} {pfid}")
        script_lines.append("quit\n")

        with tempfile.NamedTemporaryFile(
            mode="w", suffix="_steamcmd_script.txt", delete=False, encoding="utf-8"
        ) as fh:
            fh.write("\n".join(script_lines))
            return fh.name

    # ── Symlink ──────────────────────────────────────────────────────────────

    async def ensure_symlink(
        self, target: str, forced: bool = False
    ) -> None:
        """
        Symlink SteamCMD workshop content to *target*, raising
        ``SymlinkConflictError`` when a real dir or file already exists at the
        target path and *forced* is ``False``.

        When *forced* is ``True`` an existing real directory or file is
        removed before the symlink is created.
        """
        if not target:
            raise SymlinkConflictError(
                "Local mods path is not configured; cannot create SteamCMD symlink."
            )

        dst = Path(self.symlink_target)
        _remove_symlink_conflict(dst, forced)

        Path(target).mkdir(parents=True, exist_ok=True)
        dst.parent.mkdir(parents=True, exist_ok=True)
        os.symlink(target, dst, target_is_directory=True)
        msg = f"Created SteamCMD symlink: {dst} -> {target}"
        logger.debug("[steamcmd] {}", msg)
        self.status_message_changed.emit(msg)

    # ── Installation ──────────────────────────────────────────────────────────

    async def ensure_installed(
        self,
        prefix: str | None = None,
        reinstall: bool = False,
        *,
        loading_state: LoadingState | None = None,
    ) -> bool:
        if prefix:
            self._derive_paths(prefix)
            self._ctx.config.paths.steamcmd_prefix = prefix
            self._config.save()

        if self.is_installed() and not reinstall:
            logger.debug(f"[steamcmd] already installed at {self._executable}")
            return True

        system = platform.system()
        url = _STEAMCMD_URLS.get(system)
        if url is None:
            logger.warning("[steamcmd] unsupported platform: {}", system)
            self.status_message_changed.emit(
                f"SteamCMD is not supported on platform: {system}"
            )
            return False

        if loading_state is not None:
            return await self._ensure_with_progress(url, loading_state)

        self.status_message_changed.emit(f"Downloading SteamCMD from {url}...")
        try:
            data = await _download_bytes(url)
            self.status_message_changed.emit("Extracting SteamCMD...")
            await asyncio.to_thread(_extract_archive, data, url, self._install_path)
        except Exception as e:  # noqa: BLE001
            logger.error("[steamcmd] download/extraction failed: {}", e)
            self.status_message_changed.emit(
                f"Failed to install SteamCMD ({type(e).__name__}): {e}"
            )
            return False

        if self.is_installed():
            self.status_message_changed.emit("SteamCMD installed successfully.")
            return True
        self.status_message_changed.emit(
            "SteamCMD installation completed but executable not found."
        )
        return False

    async def _ensure_with_progress(
        self, url: str, loading_state: LoadingState
    ) -> bool:
        """Download and extract SteamCMD with per-stage LoadingState progress."""
        with loading_state.task("SteamCMD", total_steps=2):
            loading_state.step()

            with loading_state.task("Downloading SteamCMD\u2026", total_steps=1):
                self.status_message_changed.emit(f"Downloading SteamCMD from {url}...")
                try:
                    data = await _download_bytes(url)
                except (httpx.HTTPError, OSError) as e:
                    logger.error("[steamcmd] download failed: {}", e)
                    self.status_message_changed.emit(
                        f"Failed to install SteamCMD ({type(e).__name__}): {e}"
                    )
                    return False
                loading_state.step()

            with loading_state.task("Extracting SteamCMD\u2026", total_steps=1):
                self.status_message_changed.emit("Extracting SteamCMD...")
                try:
                    await asyncio.to_thread(
                        _extract_archive, data, url, self._install_path
                    )
                except (ValueError, OSError) as e:
                    logger.error("[steamcmd] extraction failed: {}", e)
                    self.status_message_changed.emit(
                        f"Failed to install SteamCMD ({type(e).__name__}): {e}"
                    )
                    return False
                loading_state.step()

            loading_state.step()

            if self.is_installed():
                self.status_message_changed.emit("SteamCMD installed successfully.")
                return True
            self.status_message_changed.emit(
                "SteamCMD installation completed but executable not found."
            )
            return False

    # ── Download ────────────────────────────────────────────────────────────

    async def download_mods(
        self,
        publishedfileids: list[str],
        validate: bool,
        titles: dict[str, str] | None = None,
    ) -> None:
        if not publishedfileids:
            raise ValueError("No mods selected for download.")
        if not self.is_installed():
            raise ValueError("SteamCMD is not installed; cannot download mods.")

        titles = titles or {}
        batches = self.split_batches(publishedfileids)
        logger.info(
            f"[steamcmd] download_mods: {len(publishedfileids)} items"
            f" in {len(batches)} batches"
        )
        from pxmodrim.core.services.steam_cmd_worker import (
            SteamCmdDownloadWorker,
        )

        def _script_builder(batch: list[str]) -> str:
            return self._build_download_script(batch, validate=validate)

        factory = self._runner_factory or SteamCmdDownloadWorker
        worker = factory(
            self._executable,
            self._steam_path,
            batches,
            _script_builder,
            None,
        )
        self._worker = worker

        done = asyncio.Event()

        def _on_status(msg: str) -> None:
            self.status_message_changed.emit(msg)

        def _on_progress(total: int, completed: int, _id: str, _t: str) -> None:
            self.download_progress.emit(
                SteamCmdProgress(
                    total=total,
                    completed=completed,
                    current_id=_id,
                    current_title=titles.get(_id, ""),
                )
            )

        def _on_item(pid: str, status: str) -> None:
            self.download_item_status_changed.emit(
                SteamCmdItemStatus(mod_id=pid, status=status)
            )

        def _on_finished(succeeded: list[str], failed: list[str]) -> None:
            self.download_finished.emit(
                SteamCmdResult(succeeded=succeeded, failed=failed)
            )
            done.set()

        worker.status.connect(_on_status)
        worker.progress.connect(_on_progress)
        worker.item_status.connect(_on_item)
        worker.finished.connect(_on_finished)

        worker.start()
        try:
            await done.wait()
        finally:
            worker.wait()
            worker.quit()
            self._worker = None

    def cancel(self) -> None:
        logger.debug("[steamcmd] cancel requested")
        worker = getattr(self, "_worker", None)
        if worker is not None:
            worker.cancel()

    # ── Lifecycle ─────────────────────────────────────────────────────────────

    async def close(self) -> None:
        self.cancel()
        logger.debug("[steamcmd] service closed")
