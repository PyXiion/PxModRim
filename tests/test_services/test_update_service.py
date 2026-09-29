from __future__ import annotations

import httpx
import pytest

from pxmodrim.core.services.update_service import (
    UpdateCheckError,
    UpdateService,
    is_newer,
    parse_version,
)


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
        ("v0.2.0", "0.2.0-rc1", True),
        ("v0.2.0-rc1", "0.2.0", False),
        ("v0.2.0-rc2", "0.2.0-rc1", True),
        ("v0.2.0-rc1", "0.2.0-rc1", False),
        ("v0.2.0-rc1", "0.1.9", True),
        ("v0.1.9", "0.2.0-rc1", False),
        ("v0.2.0+build5", "0.2.0", False),
    ],
)
def test_is_newer(tag: str, current: str, expected: bool) -> None:
    assert is_newer(tag, current) is expected


def test_parse_version_rejects_non_numeric() -> None:
    assert parse_version("latest") is None
    assert parse_version(" v1.2.3 ") == (1, 2, 3)


def _service(transport: httpx.MockTransport, current: str = "0.1.0") -> UpdateService:
    return UpdateService(current, transport=transport)


def _json_transport(payload: object, status: int = 200) -> httpx.MockTransport:
    return httpx.MockTransport(lambda request: httpx.Response(status, json=payload))


_RELEASE = {
    "tag_name": "v0.2.0",
    "html_url": "https://example.test/r/v0.2.0",
    "name": "Second",
    "body": "notes",
}


async def test_check_returns_newer_release() -> None:
    release = await _service(_json_transport(_RELEASE)).check()

    assert release is not None
    assert release.tag == "v0.2.0"
    assert release.name == "Second"
    assert release.notes == "notes"
    assert release.url == "https://example.test/r/v0.2.0"


async def test_check_returns_none_when_not_newer() -> None:
    assert await _service(_json_transport(_RELEASE), "0.2.0").check() is None


async def test_check_offers_release_over_its_rc() -> None:
    service = _service(_json_transport(_RELEASE), "0.2.0-rc1")

    release = await service.check()

    assert release is not None
    assert release.tag == "v0.2.0"


async def test_check_wraps_http_status_error() -> None:
    with pytest.raises(UpdateCheckError):
        await _service(_json_transport({}, status=500)).check()


async def test_check_wraps_transport_error() -> None:
    def fail(request: httpx.Request) -> httpx.Response:
        raise httpx.ConnectError("down", request=request)

    with pytest.raises(UpdateCheckError):
        await _service(httpx.MockTransport(fail)).check()


async def test_check_wraps_non_json_body() -> None:
    transport = httpx.MockTransport(lambda request: httpx.Response(200, text="<html>"))

    with pytest.raises(UpdateCheckError):
        await _service(transport).check()


@pytest.mark.parametrize("payload", [[], None, "tag", 3])
async def test_check_rejects_non_object_json(payload: object) -> None:
    with pytest.raises(UpdateCheckError):
        await _service(_json_transport(payload)).check()


@pytest.mark.parametrize(
    "payload",
    [
        {},
        {"tag_name": "v0.2.0"},
        {"html_url": "https://example.test/r"},
        {"tag_name": "", "html_url": "https://example.test/r"},
    ],
)
async def test_check_rejects_missing_fields(payload: dict[str, str]) -> None:
    with pytest.raises(UpdateCheckError):
        await _service(_json_transport(payload)).check()
