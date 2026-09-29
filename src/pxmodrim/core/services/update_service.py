from __future__ import annotations

import re
from dataclasses import dataclass

import httpx
from loguru import logger

LATEST_RELEASE_URL = "https://api.github.com/repos/PyXiion/PxModRim/releases/latest"

_TIMEOUT_SECONDS = 10.0
_VERSION_RE = re.compile(r"^v?(\d+(?:\.\d+)*)")


@dataclass(frozen=True, slots=True)
class ReleaseInfo:
    tag: str
    name: str
    notes: str
    url: str


def parse_version(text: str) -> tuple[int, ...] | None:
    """Parse ``v1.2.3``-style text into a numeric tuple, ignoring any suffix."""
    match = _VERSION_RE.match(text.strip())
    if match is None:
        return None
    return tuple(int(part) for part in match.group(1).split("."))


def is_newer(tag: str, current: str) -> bool:
    """Return True when *tag* is a strictly higher release than *current*.

    Unparseable versions (e.g. an unknown dev build) are never reported as
    outdated, so a broken tag cannot nag the user.
    """
    latest = parse_version(tag)
    running = parse_version(current)
    if latest is None or running is None:
        return False
    width = max(len(latest), len(running))
    pad = (0,) * width
    return latest + pad[len(latest) :] > running + pad[len(running) :]


class UpdateService:
    """Looks up the newest published GitHub release."""

    __slots__ = ("_current",)

    def __init__(self, current_version: str) -> None:
        self._current = current_version

    @property
    def current_version(self) -> str:
        return self._current

    async def check(self) -> ReleaseInfo | None:
        """Return the latest release if newer than the running app, else None.

        Raises ``httpx.HTTPError`` on network failure so callers can decide
        whether to surface it.
        """
        async with httpx.AsyncClient(
            timeout=_TIMEOUT_SECONDS,
            headers={
                "Accept": "application/vnd.github+json",
                "User-Agent": f"PxModRim/{self._current}",
            },
        ) as client:
            response = await client.get(LATEST_RELEASE_URL)
            response.raise_for_status()
            data = response.json()

        tag = str(data.get("tag_name") or "")
        url = str(data.get("html_url") or "")
        if not tag or not url:
            logger.warning("Latest release response lacks tag_name/html_url")
            return None
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
