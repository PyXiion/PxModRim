from __future__ import annotations

from typing import Any

import toposort
from loguru import logger


class Plugin:
    """
    Base class for all plugins.

    ``setup(ctx)`` is called synchronously during ``setup_all()`` in dependency
    order.  Use it to register views, connect signals, etc.  ``init(ctx)`` is
    async and runs immediately after ``setup()`` — use it for I/O or async init.
    """

    name: str = ""
    dependencies: list[str] = []

    def setup(self, ctx: Any) -> None: ...

    async def init(self, ctx: Any) -> None: ...

    async def shutdown(self) -> None: ...


class PluginRegistry:
    def __init__(self) -> None:
        """Initialize the plugin registry with an empty plugin map."""
        self._plugins: dict[str, Plugin] = {}

    def register(self, plugin: Plugin, ctx: Any = None) -> None:
        logger.debug("plugin registered: {}", plugin.name)
        self._plugins[plugin.name] = plugin

    def get[T](self, name: str) -> T | None:  # type: ignore[reportInvalidTypeVarUse]
        return self._plugins.get(name)

    @property
    def names(self) -> set[str]:
        return set(self._plugins)

    def setup_all(self, ctx: Any, extra_deps: set[str] | None = None) -> None:
        available = extra_deps or set()
        for name, plugin in self._plugins.items():
            for dep in plugin.dependencies:
                if dep not in self._plugins and dep not in available:
                    raise RuntimeError(
                        f"Plugin '{name}' depends on '{dep}' which is not registered"
                    )
        logger.info("setting up {} plugins...", len(self._plugins))
        for p in self._toposort():
            p.setup(ctx)
        logger.info("all plugins set up")

    async def init_all(self, ctx: Any) -> None:
        logger.info("initializing {} plugins...", len(self._plugins))
        for p in self._toposort():
            await p.init(ctx)
        logger.info("all plugins initialized")

    def _toposort(self) -> list[Plugin]:
        data = {
            name: {
                dep for dep in plugin.dependencies
                if dep in self._plugins
            }
            for name, plugin in self._plugins.items()
        }
        levels = list(toposort.toposort(data))
        return [self._plugins[name] for level in levels for name in sorted(level)]

    async def shutdown_all(self) -> None:
        logger.info("shutting down {} plugins...", len(self._plugins))
        for p in reversed(list(self._plugins.values())):
            await p.shutdown()
        logger.info("all plugins shut down")
