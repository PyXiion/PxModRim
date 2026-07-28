from __future__ import annotations

import asyncio
import os
from unittest.mock import patch

import pytest

from pxmodrim._app import _parse_disabled_plugins
from pxmodrim.core.plugin import Plugin, PluginRegistry


class _RecorderPlugin(Plugin):
    def __init__(self, name: str, dependencies: list[str] | None = None) -> None:
        self.name = name
        self.dependencies = dependencies or []

    def setup(self, ctx: list[str]) -> None:
        ctx.append(self.name)

    async def init(self, ctx: list[str]) -> None:
        ctx.append(self.name)


class TestPluginRegistryToposort:
    def test_empty_registry(self) -> None:
        reg = PluginRegistry()
        order: list[str] = []
        reg.setup_all(order)
        assert order == []

    def test_no_dependencies(self) -> None:
        reg = PluginRegistry()
        reg.register(_RecorderPlugin("a"))
        reg.register(_RecorderPlugin("b"))
        reg.register(_RecorderPlugin("c"))
        order: list[str] = []
        reg.setup_all(order)
        assert order == ["a", "b", "c"]

    def test_linear_chain(self) -> None:
        reg = PluginRegistry()
        reg.register(_RecorderPlugin("a"))
        reg.register(_RecorderPlugin("b", ["a"]))
        reg.register(_RecorderPlugin("c", ["b"]))
        order: list[str] = []
        reg.setup_all(order)
        assert order == ["a", "b", "c"]

    def test_complex_dag(self) -> None:
        reg = PluginRegistry()
        reg.register(_RecorderPlugin("a"))
        reg.register(_RecorderPlugin("b", ["a"]))
        reg.register(_RecorderPlugin("c", ["a"]))
        reg.register(_RecorderPlugin("d", ["b", "c"]))
        reg.register(_RecorderPlugin("e", ["d"]))
        reg.register(_RecorderPlugin("f"))
        reg.register(_RecorderPlugin("g", ["e", "f"]))
        order: list[str] = []
        reg.setup_all(order)
        assert order == ["a", "f", "b", "c", "d", "e", "g"]

    def test_missing_dependency_raises(self) -> None:
        reg = PluginRegistry()
        reg.register(_RecorderPlugin("a", ["nonexistent"]))
        with pytest.raises(RuntimeError, match="depends on 'nonexistent'"):
            reg.setup_all(None)

    def test_extra_deps(self) -> None:
        reg = PluginRegistry()
        reg.register(_RecorderPlugin("a", ["external_dep"]))
        order: list[str] = []
        reg.setup_all(order, extra_deps={"external_dep"})
        assert order == ["a"]

    def test_init_all_order(self) -> None:
        reg = PluginRegistry()
        reg.register(_RecorderPlugin("a"))
        reg.register(_RecorderPlugin("b", ["a"]))
        reg.register(_RecorderPlugin("c", ["b"]))
        order: list[str] = []
        asyncio.run(reg.init_all(order))
        assert order == ["a", "b", "c"]


class TestParseDisabledPlugins:
    def test_empty(self) -> None:
        with patch.dict(os.environ, {"PX_DISABLED_PLUGINS": ""}):
            assert _parse_disabled_plugins() == set()

    def test_single(self) -> None:
        with patch.dict(os.environ, {"PX_DISABLED_PLUGINS": "steamcmd"}):
            assert _parse_disabled_plugins() == {"steamcmd"}

    def test_multiple(self) -> None:
        with patch.dict(os.environ, {"PX_DISABLED_PLUGINS": "a, b, c"}):
            assert _parse_disabled_plugins() == {"a", "b", "c"}

    def test_empty_entries(self) -> None:
        with patch.dict(os.environ, {"PX_DISABLED_PLUGINS": "a,,b"}):
            assert _parse_disabled_plugins() == {"a", "b"}

    def test_whitespace_around_names(self) -> None:
        with patch.dict(
            os.environ,
            {"PX_DISABLED_PLUGINS": "  steamcmd , steamworkshop  "},
        ):
            assert _parse_disabled_plugins() == {"steamcmd", "steamworkshop"}

    def test_not_set(self) -> None:
        with patch.dict(os.environ, {"PX_DISABLED_PLUGINS": ""}):
            os.environ.pop("PX_DISABLED_PLUGINS", None)
            assert _parse_disabled_plugins() == set()
