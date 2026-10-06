from __future__ import annotations

from collections.abc import Callable
from typing import TYPE_CHECKING, ClassVar, Protocol

from PySide6.QtWidgets import QWidget

from pxmodrim.core.plugin import Plugin, PluginRegistry
from pxmodrim.ui.ui_prefs import UIPrefs

if TYPE_CHECKING:
    from PySide6.QtQml import QQmlEngine

    from pxmodrim.core.context import CoreContext


class Notifier(Protocol):
    def info(self, message: str, duration: int = ...) -> None: ...
    def success(self, message: str, duration: int = ...) -> None: ...
    def warning(self, message: str, duration: int = ...) -> None: ...
    def error(self, message: str, duration: int = ...) -> None: ...


class RailView(QWidget):
    """Base of widgets registered on the navigation rail."""

    view_id: ClassVar[str]
    icon_name: ClassVar[str]
    label: ClassVar[str]

    def open_route(self, path: tuple[str, ...]) -> None:
        """Handle the part of a ``modrim://<view_id>/…`` link after the view id."""

    def __init__(
        self,
        ctx: CoreContext,
        qml_engine: QQmlEngine | None = None,
        parent: QWidget | None = None,
        app_ctx: AppContext | None = None,
    ) -> None:
        super().__init__(parent)


class AppContext:
    __slots__ = (
        "_core",
        "_navigate",
        "_plugins",
        "_rail_views",
        "_toasts",
        "_ui_prefs",
    )

    def __init__(self, core: CoreContext, ui_prefs: UIPrefs | None = None) -> None:
        self._core = core
        self._plugins = PluginRegistry()
        self._rail_views: list[type[RailView]] = []
        self._toasts: Notifier | None = None
        self._navigate: Callable[[str], None] | None = None
        self._ui_prefs = ui_prefs or UIPrefs()

    # ── Plugin system (UI layer) ──────────────────────

    def register_plugin(self, plugin: Plugin) -> None:
        self._plugins.register(plugin)

    @property
    def plugins(self) -> PluginRegistry:
        return self._plugins

    # ── Rail views ────────────────────────────────────

    def add_rail_view(
        self, view_cls: type[RailView], *, position: int | None = None
    ) -> None:
        if position is None:
            self._rail_views.append(view_cls)
        else:
            self._rail_views.insert(position, view_cls)

    @property
    def rail_views(self) -> tuple[type[RailView], ...]:
        return tuple(self._rail_views)

    # ── Toasts ────────────────────────────────────────

    @property
    def toasts(self) -> Notifier | None:
        return self._toasts

    @toasts.setter
    def toasts(self, notifier: Notifier | None) -> None:
        self._toasts = notifier

    # ── Navigation ────────────────────────────────────

    def navigate(self, url: str) -> None:
        """Open a ``modrim://<view_id>[/…]`` link (no-op if the view is unknown)."""
        if self._navigate is not None:
            self._navigate(url)

    def set_navigator(self, navigate: Callable[[str], None] | None) -> None:
        self._navigate = navigate

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

    async def refresh_mods(self, full: bool = False) -> int:
        await self._core.mod_service.reload(full=full)
        return len(self._core.all_mods)

    async def shutdown_all(self) -> None:
        await self._plugins.shutdown_all()
        await self._core.plugins.shutdown_all()
