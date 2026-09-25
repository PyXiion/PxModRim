from __future__ import annotations

import asyncio
import json
from typing import TYPE_CHECKING

from PySide6.QtCore import QObject, Signal, Slot

if TYPE_CHECKING:
    from pxmodrim.ui.plugins.steam_workshop.plugin import SteamCmdUiPlugin


def _is_published_file_id(value: object) -> bool:
    return isinstance(value, str) and value.isascii() and value.isdecimal()


class PxModRimBridge(QObject):
    result_ready = Signal(str, str)

    def __init__(self, plugin: SteamCmdUiPlugin) -> None:
        super().__init__()
        self._plugin = plugin

    @Slot(str, str, str)
    def call(self, request_id: str, method: str, payload: str) -> None:
        asyncio.create_task(self._handle(request_id, method, payload))

    async def _handle(self, request_id: str, method: str, payload: str) -> None:
        try:
            params = json.loads(payload) if payload else {}

            if method == "init_ready":
                self._plugin.sync_all()
                result = None
            elif method == "toggle_download_checked":
                mod_id = params["mod_id"]
                if _is_published_file_id(mod_id):
                    self._plugin.toggle_download_checked(
                        mod_id,
                        params.get("title", ""),
                        params.get("checked", False),
                    )
                result = None
            elif method == "batch_toggle_download_checked":
                valid_items = [
                    (mod_id, title)
                    for mod_id, title in zip(
                        params["mod_ids"], params["titles"], strict=True
                    )
                    if _is_published_file_id(mod_id)
                ]
                if valid_items:
                    self._plugin.batch_toggle_download_checked(
                        [mod_id for mod_id, _title in valid_items],
                        [title for _mod_id, title in valid_items],
                        params.get("checked", False),
                    )
                result = None
            elif method == "toggle_active":
                mod_id = params["mod_id"]
                if _is_published_file_id(mod_id):
                    self._plugin.toggle_active(
                        mod_id,
                        params.get("active", False),
                    )
                result = None
            elif method == "batch_toggle_active":
                mod_ids = [
                    mod_id
                    for mod_id in params["mod_ids"]
                    if _is_published_file_id(mod_id)
                ]
                if mod_ids:
                    self._plugin.batch_toggle_active(
                        mod_ids,
                        params.get("active", False),
                    )
                result = None
            elif method == "fetch_mod_deps":
                mod_id = params["mod_id"]
                if _is_published_file_id(mod_id):
                    raw = await asyncio.wait_for(
                        self._plugin.fetch_mod_deps(mod_id),
                        timeout=30,
                    )
                    result = json.loads(raw) if raw else None
                else:
                    result = None
            else:
                raise RuntimeError(f"Unknown method: {method}")

            self.result_ready.emit(
                request_id,
                json.dumps({"ok": True, "result": result}),
            )
        except TimeoutError:
            self.result_ready.emit(
                request_id,
                json.dumps({"ok": False, "error": "Timeout"}),
            )
        except Exception as exc:
            self.result_ready.emit(
                request_id,
                json.dumps({"ok": False, "error": str(exc)}),
            )
