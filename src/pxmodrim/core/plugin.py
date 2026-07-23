from __future__ import annotations

from typing import Any, cast

from loguru import logger


class Plugin:

    """
    Base class for all plugins.

    ``setup(ctx)`` is called synchronously at register time.  Use it to
    register views, connect signals, etc.  ``init(ctx)`` is async and runs
    during startup — use it for I/O or async init tasks.
    """

    name: str = ""

    def setup(self, ctx: Any) -> None:
        ...

    async def init(self, ctx: Any) -> None:
        ...

    async def shutdown(self) -> None:
        ...


class PluginRegistry:
    def __init__(self) -> None:
        """Initialize the plugin registry with an empty plugin map."""
        self._plugins: dict[str, Plugin] = {}

    def register(self, plugin: Plugin, ctx: Any = None) -> None:
        logger.debug("plugin registered: {}", plugin.name)
        self._plugins[plugin.name] = plugin
        if ctx is not None:
            plugin.setup(ctx)

    def get[T](self, name: str) -> T | None:  # type: ignore[reportInvalidTypeVarUse]
        return cast(T | None, self._plugins.get(name))

    async def init_all(self, ctx: Any) -> None:
        logger.info("initializing {} plugins...", len(self._plugins))
        for p in self._plugins.values():
            await p.init(ctx)
        logger.info("all plugins initialized")

    async def shutdown_all(self) -> None:
        logger.info("shutting down {} plugins...", len(self._plugins))
        for p in reversed(list(self._plugins.values())):
            await p.shutdown()
        logger.info("all plugins shut down")
