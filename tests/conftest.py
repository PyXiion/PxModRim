from __future__ import annotations

import gc
from collections.abc import Iterator
from pathlib import Path

import pytest

from pxmodrim.core.config import ConfigService


@pytest.fixture
def config_service(tmp_path: Path) -> ConfigService:
    """A ``ConfigService`` backed by a temporary directory.

    All services that would normally use ``~/.config/pxmodrim``
    are isolated to this temp directory instead.
    """
    return ConfigService(tmp_path)


@pytest.fixture(autouse=True, scope="module")
def _collect_qobjects() -> Iterator[None]:
    """Destroy unparented QObjects at module end, not at an arbitrary later GC."""
    yield
    gc.collect()
