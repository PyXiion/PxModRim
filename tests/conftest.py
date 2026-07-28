from __future__ import annotations

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
