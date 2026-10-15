from __future__ import annotations

from dataclasses import dataclass
from urllib.parse import quote, unquote, urlsplit

SCHEME = "modrim"
SETTINGS_VIEW_ID = "settings"


@dataclass(frozen=True, slots=True)
class Route:
    view_id: str
    path: tuple[str, ...] = ()

    @property
    def url(self) -> str:
        return build_route(self.view_id, *self.path)


def build_route(view_id: str, *path: str) -> str:
    segments = "".join(f"/{quote(part, safe='')}" for part in path)
    return f"{SCHEME}://{view_id}{segments}"


def parse_route(url: str) -> Route | None:
    """Parse ``modrim://<view>[/<part>…]``; anything else is not a route."""
    try:
        parts = urlsplit(url)
    except ValueError:
        return None
    if parts.scheme != SCHEME or not parts.netloc:
        return None
    path = tuple(unquote(part) for part in parts.path.split("/") if part)
    return Route(parts.netloc, path)
