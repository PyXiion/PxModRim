from __future__ import annotations

import re
from dataclasses import dataclass

import httpx
from loguru import logger

LATEST_RELEASE_URL = "https://api.github.com/repos/PyXiion/PxModRim/releases/latest"

_TIMEOUT_SECONDS = 10.0
_VERSION_RE = re.compile(r"^v?(\d+(?:\.\d+)*)([^+]*)")


class UpdateCheckError(Exception):
    """The latest-release lookup failed (network, HTTP status or bad payload)."""


@dataclass(frozen=True, slots=True)
class ReleaseInfo:
    tag: str
    name: str
    notes: str
    url: str


def _split_version(text: str) -> tuple[tuple[int, ...], str] | None:
    match = _VERSION_RE.match(text.strip())
    if match is None:
        return None
    numbers = tuple(int(part) for part in match.group(1).split("."))
    return numbers, match.group(2).strip("-._ ").lower()


def parse_version(text: str) -> tuple[int, ...] | None:
    """Parse ``v1.2.3``-style text into a numeric tuple, ignoring any suffix."""
    parsed = _split_version(text)
    return None if parsed is None else parsed[0]


def is_newer(tag: str, current: str) -> bool:
    """Return True when *tag* is a strictly higher release than *current*.

    A pre-release suffix (``-rc1``, ``-beta.1``) sorts below the plain release
    with the same numbers. Unparseable versions (e.g. an unknown dev build) are
    never reported as outdated, so a broken tag cannot nag the user.
    """
    latest = _split_version(tag)
    running = _split_version(current)
    if latest is None or running is None:
        return False
    width = max(len(latest[0]), len(running[0]))

    def key(parsed: tuple[tuple[int, ...], str]) -> tuple[tuple[int, ...], bool, str]:
        numbers, suffix = parsed
        padded = numbers + (0,) * (width - len(numbers))
        return padded, not suffix, suffix

    return key(latest) > key(running)


class UpdateService:
    """Looks up the newest published GitHub release."""

    __slots__ = ("_current", "_transport")

    def __init__(
        self,
        current_version: str,
        transport: httpx.AsyncBaseTransport | None = None,
    ) -> None:
        self._current = current_version
        self._transport = transport

    @property
    def current_version(self) -> str:
        return self._current

    async def check(self) -> ReleaseInfo | None:
        """Return the latest release if newer than the running app, else None.

        Raises ``UpdateCheckError`` on network failure, a non-success HTTP
        status or a malformed response.
        """
        try:
            async with httpx.AsyncClient(
                timeout=_TIMEOUT_SECONDS,
                headers={
                    "Accept": "application/vnd.github+json",
                    "User-Agent": f"PxModRim/{self._current}",
                },
                transport=self._transport,
            ) as client:
                response = await client.get(LATEST_RELEASE_URL)
                response.raise_for_status()
                data = response.json()
        except (httpx.HTTPError, ValueError) as exc:
            raise UpdateCheckError(str(exc) or type(exc).__name__) from exc

        if not isinstance(data, dict):
            raise UpdateCheckError("Latest release response is not a JSON object")
        tag = str(data.get("tag_name") or "")
        url = str(data.get("html_url") or "")
        if not tag or not url:
            raise UpdateCheckError("Latest release response lacks tag_name/html_url")
        if not is_newer(tag, self._current):
            logger.debug("No update: latest {} vs running {}", tag, self._current)
            return None
        logger.info("Update available: {} (running {})", tag, self._current)
        return ReleaseInfo(
            tag=tag,
            name=str(data.get("name") or tag),
            notes=str(data.get("body") or ""),
            url=url,
        )
