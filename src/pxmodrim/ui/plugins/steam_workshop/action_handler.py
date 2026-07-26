from __future__ import annotations

import json
from collections.abc import Callable
from typing import TYPE_CHECKING
from urllib.parse import parse_qs, unquote

from PySide6.QtCore import QBuffer
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
        }

    def requestStarted(self, job: _Job) -> None:
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
        if mod_id:
            self._plugin.fetch_mod_deps(mod_id)
        self._reply_json(job, {"ok": True})
