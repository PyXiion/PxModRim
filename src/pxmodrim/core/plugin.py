from __future__ import annotations

from typing import Any, cast


class Plugin:

    """
    Base class for all plugins.

    ``setup(ctx)`` is called synchronously at register time.  Use it to
    register views, connect signals, etc.  ``init(ctx)`` is async and runs
    during startup — use it for I/O or async init tasks.
    """

    name: str = ""

    def setup(self, ctx: Any) -> None: ...
    async def init(self, ctx: Any) -> None: ...
    async def shutdown(self) -> None: ...


class PluginRegistry:
    def __init__(self) -> None:
        """Initialize the plugin registry with an empty plugin map."""
        self._plugins: dict[str, Plugin] = {}

    def register(self, plugin: Plugin, ctx: Any = None) -> None:
        self._plugins[plugin.name] = plugin
        if ctx is not None:
            plugin.setup(ctx)

    def get[T](self, name: str) -> T | None:  # type: ignore[reportInvalidTypeVarUse]
        return cast(T | None, self._plugins.get(name))

    async def init_all(self, ctx: Any) -> None:
        for p in self._plugins.values():
            await p.init(ctx)

    async def shutdown_all(self) -> None:
        for p in reversed(list(self._plugins.values())):
            await p.shutdown()
