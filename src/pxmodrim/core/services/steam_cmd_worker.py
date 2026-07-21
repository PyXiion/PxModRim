from __future__ import annotations

import contextlib
import os
import re
import subprocess
from typing import Any

from loguru import logger
from PySide6.QtCore import QThread, Signal

_DOWNLOADING_RE = re.compile(r"Downloading item (\d+)\.\.\.")
_SUCCESS_RE = re.compile(r"Success\. Downloaded item (\d+)")
_ERROR_RE = re.compile(r"ERROR! Download item (\d+)")
_LOGON_RE = re.compile(r"ERROR! Not logged on\.")


def _kill(proc: subprocess.Popen[str]) -> None:
    try:
        if proc.poll() is None:
            if hasattr(os, "killpg"):
                os.killpg(os.getpgid(proc.pid), 15)
            else:
                proc.terminate()
    except OSError:
        with contextlib.suppress(OSError):
            proc.kill()


class SteamCmdDownloadWorker(QThread):
    status = Signal(str)
    progress = Signal(int, int, str, str)
    item_status = Signal(str, str)
    finished = Signal(list, list)

    def __init__(
        self,
        steamcmd: str,
        steam_path: str,
        batches: list[list[str]],
        script_builder,
        parent: Any = None,
    ) -> None:
        """Initialize the SteamCMD download worker thread."""
        super().__init__(parent)
        self._steamcmd = steamcmd
        self._steam_path = steam_path
        self._batches = batches
        self._script_builder = script_builder
        self._stopped = False

    def cancel(self) -> None:
        self._stopped = True

    def run(self) -> None:
        succeeded: list[str] = []
        failed: list[str] = []
        total = sum(len(b) for b in self._batches)
        completed = 0
        logger.info(
            f"[steamcmd] worker started: {total} items in {len(self._batches)} batches"
        )
        proc: subprocess.Popen[str] | None = None
        try:
            for batch in self._batches:
                if self._stopped:
                    break
                os.makedirs(self._steam_path, exist_ok=True)
                script = self._script_builder(batch)
                logger.debug(f"[steamcmd] spawning: {self._steamcmd} batch={batch}")
                proc = subprocess.Popen(
                    [self._steamcmd, f'+runscript "{script}"'],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    cwd=self._steam_path,
                    start_new_session=True,
                )
                assert proc.stdout is not None
                for line in proc.stdout:
                    if self._stopped:
                        _kill(proc)
                        break
                    self._parse_line(line, succeeded, failed)
                    completed = len(succeeded) + len(failed)
                    self.progress.emit(
                        total, completed, "", ""
                    )
                proc.wait()
                proc = None

                with contextlib.suppress(OSError):
                    os.remove(script)
        except Exception as e:  # noqa: BLE001
            self.status.emit(f"SteamCMD worker error: {type(e).__name__}: {e}")
        finally:
            if proc is not None:
                _kill(proc)
        logger.info(
            f"[steamcmd] worker done: {len(succeeded)} ok, {len(failed)} failed"
        )
        self.finished.emit(succeeded, failed)

    def _parse_line(
        self, line: str, succeeded: list[str], failed: list[str]
    ) -> None:
        text = line.strip()
        m = _DOWNLOADING_RE.search(text)
        if m:
            self.item_status.emit(m.group(1), "downloading")
            return
        m = _SUCCESS_RE.search(text)
        if m:
            pid = m.group(1)
            if pid not in succeeded:
                succeeded.append(pid)
            self.item_status.emit(pid, "success")
            return
        m = _ERROR_RE.search(text)
        if m:
            pid = m.group(1)
            if pid not in failed:
                failed.append(pid)
            self.item_status.emit(pid, "error")
            return
        if _LOGON_RE.search(text):
            self.status.emit("SteamCMD failed to log in anonymously.")
            return
        logger.trace(f"[steamcmd] {text}")
