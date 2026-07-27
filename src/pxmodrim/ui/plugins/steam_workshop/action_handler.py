from __future__ import annotations

import asyncio
import json
from collections.abc import Callable
from typing import TYPE_CHECKING
from urllib.parse import parse_qs, unquote

from loguru import logger
from PySide6.QtCore import QBuffer, QByteArray, QTimer
from PySide6.QtWebEngineCore import (
    QWebEngineUrlRequestJob,
    QWebEngineUrlSchemeHandler,
)

if TYPE_CHECKING:
    from pxmodrim.ui.plugins.steam_workshop.plugin import SteamCmdUiPlugin

_Job = QWebEngineUrlRequestJob


class SteamWorkshopActionHandler(QWebEngineUrlSchemeHandler):
    def __init__(self, plugin: SteamCmdUiPlugin) -> None:
        super().__init__(None)
        self._plugin = plugin
        self._actions: dict[str, Callable[[_Job, dict], None]] = {
            "toggle_download_checked": self._handle_toggle_download_checked,
            "batch_toggle_download_checked": self._handle_batch_toggle_download_checked,
            "fetch_mod_deps": self._handle_fetch_mod_deps,
            "initReady": self._handle_init_ready,
        }

    def requestStarted(self, job: _Job) -> None:
        logger.info("Request started: {}", job.requestUrl().toString())
        url = job.requestUrl()
        parts = url.path().strip("/").split("/")
        if not parts:
            job.fail(QWebEngineUrlRequestJob.Error.RequestFailed)
            return
        action = parts[0]

        params: dict[str, str] = {}
        query = url.query()
        if query:
            for k, v in parse_qs(query).items():
                if v:
                    params[k] = unquote(v[0])

        handler = self._actions.get(action)
        if handler is None:
            job.fail(QWebEngineUrlRequestJob.Error.RequestFailed)
            return

        handler(job, params)

    def _reply_json(self, job: _Job, data: object) -> None:
        job.setAdditionalResponseHeaders({
            QByteArray(b"Access-Control-Allow-Origin"): QByteArray(b"*"),
            QByteArray(b"Access-Control-Allow-Methods"): QByteArray(b"GET"),
        })
        buf = QBuffer(parent=self)
        buf.open(QBuffer.OpenModeFlag.ReadWrite)
        buf.write(json.dumps(data).encode("utf-8"))
        buf.seek(0)
        job.reply(b"application/json", buf)

    # ── Action handlers ──────────────────────────────────

    def _handle_toggle_download_checked(self, job: _Job, params: dict) -> None:
        mod_id = params.get("mod_id", "")
        title = params.get("title", "")
        checked = params.get("checked", "0") == "1"
        if mod_id:
            self._plugin.toggle_download_checked(mod_id, title, checked)
        self._reply_json(job, {"ok": True})

    def _handle_batch_toggle_download_checked(self, job: _Job, params: dict) -> None:
        mod_ids = json.loads(params.get("mod_ids", "[]"))
        titles = json.loads(params.get("titles", "[]"))
        checked = params.get("checked", "0") == "1"
        if mod_ids:
            self._plugin.batch_toggle_download_checked(mod_ids, titles, checked)
        self._reply_json(job, {"ok": True})

    def _handle_fetch_mod_deps(self, job: _Job, params: dict) -> None:
        mod_id = params.get("mod_id", "")
        if not mod_id:
            self._reply_json(job, {"error": "missing mod_id"})
            return
        logger.debug("[steam] fetch_mod_deps RPC: {}", mod_id)
        QTimer.singleShot(
            0, lambda: asyncio.ensure_future(self._do_fetch_deps_reply(job, mod_id))
        )

    async def _do_fetch_deps_reply(self, job: _Job, mod_id: str) -> None:
        try:
            logger.debug("[steam] _do_fetch_deps_reply start: {}", mod_id)
            result = await asyncio.wait_for(
                self._plugin.fetch_mod_deps(mod_id), timeout=30.0
            )
            logger.debug("[steam] _do_fetch_deps_reply got result: {} chars", len(result) if result else 0)
            data = json.loads(result) if result else None
        except asyncio.TimeoutError:
            logger.warning("[steam] _do_fetch_deps_reply timeout: {}", mod_id)
            data = None
        except Exception as exc:
            logger.warning("[steam] _do_fetch_deps_reply error: {} — {}", mod_id, exc)
            data = None
        self._reply_json(job, data)
        logger.debug("[steam] _do_fetch_deps_reply done: {}", mod_id)

    def _handle_init_ready(self, job: _Job, _params: dict) -> None:
        self._plugin.sync_all()
        self._reply_json(job, {"ok": True})
