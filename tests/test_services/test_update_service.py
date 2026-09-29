from __future__ import annotations

import pytest

from pxmodrim.core.services.update_service import is_newer, parse_version


@pytest.mark.parametrize(
    ("tag", "current", "expected"),
    [
        ("v0.2.0", "0.1.0", True),
        ("v0.1.0", "0.1.0", False),
        ("v0.1.0", "0.2.0", False),
        ("v0.10.0", "0.9.0", True),
        ("v1.0", "1.0.0", False),
        ("v1.0.1", "1.0", True),
        ("v0.2.0-beta.1", "0.1.0", True),
        ("nightly", "0.1.0", False),
        ("v0.2.0", "unknown", False),
    ],
)
def test_is_newer(tag: str, current: str, expected: bool) -> None:
    assert is_newer(tag, current) is expected


def test_parse_version_rejects_non_numeric() -> None:
    assert parse_version("latest") is None
    assert parse_version(" v1.2.3 ") == (1, 2, 3)
