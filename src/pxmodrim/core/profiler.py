from __future__ import annotations

from collections.abc import Iterator
from contextlib import contextmanager
from typing import TYPE_CHECKING

from loguru import logger
from ttimer import Timer

if TYPE_CHECKING:
    pass


@contextmanager
def profile(label: str) -> Iterator[Timer]:
    """Profile a code block and log the hierarchical timing report on exit.

    Usage:
        with profile("discover") as t:
            with t("provider.core"):
                ...
            with t("resolve_active"):
                ...
    """
    timer = Timer()
    with timer(label):
        yield timer
    logger.debug("Profile report:\n{}", timer.render())
