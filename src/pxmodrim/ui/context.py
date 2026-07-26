from __future__ import annotations

from typing import TYPE_CHECKING

from pxmodrim.core.plugin import Plugin, PluginRegistry
from pxmodrim.ui.ui_prefs import UIPrefs

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext


class AppContext:
    __slots__ = ("_core", "_plugins", "_rail_views", "_ui_prefs")

    def __init__(self, core: CoreContext, ui_prefs: UIPrefs | None = None) -> None:
        self._core = core
        self._plugins = PluginRegistry()
        self._rail_views: list[type] = []
        self._ui_prefs = ui_prefs or UIPrefs()

    # ── Plugin system (UI layer) ──────────────────────

    def register_plugin(self, plugin: Plugin) -> None:
        self._plugins.register(plugin)

    @property
    def plugins(self) -> PluginRegistry:
        return self._plugins

    # ── Rail views ────────────────────────────────────

    def add_rail_view(self, view_cls: type) -> None:
        self._rail_views.append(view_cls)

    @property
    def rail_views(self) -> tuple[type, ...]:
        return tuple(self._rail_views)

    # ── UI prefs ──────────────────────────────────────

    @property
    def ui_prefs(self) -> UIPrefs:
        return self._ui_prefs

    # ── Core access ───────────────────────────────────

    @property
    def core(self) -> CoreContext:
        return self._core

    # ── Lifecycle ─────────────────────────────────────

    def setup_all(self) -> None:
        self._core.plugins.setup_all(self._core)
        core_names = self._core.plugins.names
        self._plugins.setup_all(self, extra_deps=core_names)

    async def init_all(self) -> None:
        await self._core.plugins.init_all(self._core)
        await self._plugins.init_all(self)

    async def shutdown_all(self) -> None:
        await self._plugins.shutdown_all()
        await self._core.plugins.shutdown_all()
