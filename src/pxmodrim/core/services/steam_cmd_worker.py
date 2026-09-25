from __future__ import annotations

import asyncio
import contextlib
import os
import re
import signal
from collections.abc import Callable

from loguru import logger

_DOWNLOADING_RE = re.compile(r"Downloading item (\d+)\s*[.…]{3,}")
_SUCCESS_RE = re.compile(r"Success[.…]*\s*Downloaded item (\d+)")
_ERROR_RE = re.compile(r"ERROR! Download item (\d+)")
_LOGON_RE = re.compile(r"ERROR! Not logged on\.")


class SteamCmdDownloadWorker:
    def __init__(
        self,
        steamcmd: str,
        steam_path: str,
        batches: list[list[str]],
        script_builder: Callable[[list[str]], str],
        on_status: Callable[[str], None],
        on_progress: Callable[[int, int, str, str], None],
        on_item_status: Callable[[str, str], None],
    ) -> None:
        self._steamcmd = steamcmd
        self._steam_path = steam_path
        self._batches = batches
        self._script_builder = script_builder
        self._on_status = on_status
        self._on_progress = on_progress
        self._on_item_status = on_item_status
        self._stopped = False
        self._process: asyncio.subprocess.Process | None = None
        self.succeeded: list[str] = []
        self.failed: list[str] = []

    def cancel(self) -> None:
        logger.debug("[steamcmd] worker cancel requested")
        self._stopped = True
        if self._process is not None:
            self._signal_process(self._process)

    async def run(self) -> None:
        total = sum(len(batch) for batch in self._batches)
        logger.info(
            "[steamcmd] worker started: {} items in {} batches",
            total,
            len(self._batches),
        )
        try:
            for batch in self._batches:
                if self._stopped:
                    break
                await self._run_batch(batch, total)
            stopped = self._stopped
            if (
                not stopped
                and any(self._batches)
                and not self.succeeded
                and not self.failed
            ):
                self._on_status(
                    "SteamCMD produced no recognizable output. Check logs for details."
                )
        except Exception as exc:  # noqa: BLE001 - runner boundary reports failures
            if not self._stopped:
                self._on_status(f"SteamCMD worker error: {type(exc).__name__}: {exc}")
        logger.info(
            "[steamcmd] worker done: {} ok, {} failed",
            len(self.succeeded),
            len(self.failed),
        )

    async def _run_batch(self, batch: list[str], total: int) -> None:
        os.makedirs(self._steam_path, exist_ok=True)
        script = self._script_builder(batch)
        process: asyncio.subprocess.Process | None = None
        try:
            logger.debug("[steamcmd] spawning: {} batch={}", self._steamcmd, batch)
            process = await asyncio.create_subprocess_exec(
                self._steamcmd,
                "+runscript",
                script,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,
                cwd=self._steam_path,
                start_new_session=os.name != "nt",
            )
            self._process = process
            if self._stopped:
                self._signal_process(process)

            assert process.stdout is not None
            async for line in process.stdout:
                if self._stopped:
                    break
                self._parse_line(line.decode("utf-8", errors="replace"))
                completed = len(self.succeeded) + len(self.failed)
                self._on_progress(total, completed, "", "")

            returncode = await process.wait()
            if not self._stopped and returncode != 0:
                msg = f"SteamCMD exited with code {returncode}"
                logger.warning("[steamcmd] {}", msg)
                self._on_status(msg)
        finally:
            try:
                if process is not None:
                    if process.returncode is None:
                        self._signal_process(process)
                        try:
                            await asyncio.wait_for(process.wait(), timeout=5)
                        except TimeoutError:
                            self._signal_process(process, force=True)
                            await process.wait()
                    if self._process is process:
                        self._process = None
            finally:
                with contextlib.suppress(OSError):
                    os.remove(script)

    def _signal_process(
        self, process: asyncio.subprocess.Process, *, force: bool = False
    ) -> None:
        if process.returncode is not None:
            return
        try:
            if os.name == "nt":
                if force:
                    process.kill()
                else:
                    process.terminate()
            else:
                os.killpg(
                    process.pid,
                    signal.SIGKILL if force else signal.SIGTERM,
                )
        except ProcessLookupError:
            pass
        except OSError as exc:
            logger.debug("[steamcmd] kill error (expected if already dead): {}", exc)
            with contextlib.suppress(OSError):
                if force:
                    process.kill()
                else:
                    process.terminate()

    def _parse_line(self, line: str) -> None:
        text = line.strip()
        match = _DOWNLOADING_RE.search(text)
        if match:
            mod_id = match.group(1)
            logger.debug("[steamcmd] download start: {}", mod_id)
            self._on_item_status(mod_id, "downloading")
            return
        match = _SUCCESS_RE.search(text)
        if match:
            mod_id = match.group(1)
            logger.debug("[steamcmd] download success: {}", mod_id)
            if mod_id not in self.succeeded:
                self.succeeded.append(mod_id)
            self._on_item_status(mod_id, "success")
            return
        match = _ERROR_RE.search(text)
        if match:
            mod_id = match.group(1)
            logger.debug("[steamcmd] download error: {} (raw: {})", mod_id, text)
            if mod_id not in self.failed:
                self.failed.append(mod_id)
            self._on_item_status(mod_id, "error")
            return
        if _LOGON_RE.search(text):
            logger.warning("[steamcmd] logon failed")
            self._on_status("SteamCMD failed to log in anonymously.")
            return
        if re.search(r"(?i)(error|fail|denied|timeout|unable|cannot|not found)", text):
            logger.warning("[steamcmd] {}", text)
            self._on_status(f"SteamCMD: {text}")
        else:
            logger.debug("[steamcmd] {}", text)
