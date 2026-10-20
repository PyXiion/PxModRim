from __future__ import annotations

import pytest

from pxmodrim.ui.window.geometry import fit_window_geometry


@pytest.mark.parametrize(
    ("size", "available", "position", "expected"),
    [
        ((1400, 850), (0, 0, 1920, 1080), None, (260, 115, 1400, 850)),
        ((1400, 850), (0, 0, 1280, 720), None, (20, 20, 1240, 680)),
        ((1400, 850), (0, 0, 940, 600), None, (20, 20, 900, 560)),
        ((1400, 850), (0, 0, 920, 580), None, (10, 10, 900, 560)),
        ((1400, 850), (0, 0, 800, 500), None, (0, 0, 800, 500)),
        ((400, 300), (0, 0, 1920, 1080), None, (510, 260, 900, 560)),
        ((1400, 850), (1920, 80, 1280, 720), None, (1940, 100, 1240, 680)),
        ((1400, 850), (-1920, 0, 1920, 1080), None, (-1660, 115, 1400, 850)),
        ((1000, 600), (0, 0, 1920, 1080), (100, 150), (100, 150, 1000, 600)),
        ((1000, 600), (0, 0, 1920, 1080), (-500, -500), (20, 20, 1000, 600)),
        ((1000, 600), (0, 0, 1920, 1080), (3000, 2000), (900, 460, 1000, 600)),
        ((1000, 600), (-1920, 80, 1920, 1080), (0, -500), (-1020, 100, 1000, 600)),
    ],
)
def test_window_fits_available_logical_geometry(
    size: tuple[int, int],
    available: tuple[int, int, int, int],
    position: tuple[int, int] | None,
    expected: tuple[int, int, int, int],
) -> None:
    assert fit_window_geometry(size, available, position) == expected
