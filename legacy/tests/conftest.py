from __future__ import annotations

import gc
import os
from collections.abc import Iterator
from pathlib import Path

os.environ.setdefault("QT_QUICK_CONTROLS_STYLE", "Basic")
import pytest

from pxmodrim.core.config import ConfigService

os.environ.setdefault("QT_QUICK_CONTROLS_STYLE", "Basic")


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
