from __future__ import annotations

import pytest

from pxmodrim.ui.navigation import Route, build_route, parse_route


def test_route_round_trips_ids_with_reserved_characters() -> None:
    url = build_route("workshop", "collection", "picked:a/b")
    assert url == "pxmodrim://workshop/collection/picked%3Aa%2Fb"
    assert parse_route(url) == Route("workshop", ("collection", "picked:a/b"))


def test_view_only_route_has_empty_path() -> None:
    assert parse_route("pxmodrim://downloads") == Route("downloads")
    assert parse_route("pxmodrim://downloads/") == Route("downloads")


@pytest.mark.parametrize("url", ["downloads", "https://downloads", "pxmodrim://", ""])
def test_non_routes_are_rejected(url: str) -> None:
    assert parse_route(url) is None
