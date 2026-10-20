from __future__ import annotations


def fit_window_geometry(
    size: tuple[int, int],
    available: tuple[int, int, int, int],
    position: tuple[int, int] | None = None,
) -> tuple[int, int, int, int]:
    left, top, screen_width, screen_height = available
    width = min(max(size[0], 900), max(900, screen_width - 40), screen_width)
    height = min(max(size[1], 560), max(560, screen_height - 40), screen_height)
    margin_x = min(20, (screen_width - width) // 2)
    margin_y = min(20, (screen_height - height) // 2)
    x, y = position or (
        left + (screen_width - width) // 2,
        top + (screen_height - height) // 2,
    )
    x = max(left + margin_x, min(x, left + screen_width - width - margin_x))
    y = max(top + margin_y, min(y, top + screen_height - height - margin_y))
    return x, y, width, height
