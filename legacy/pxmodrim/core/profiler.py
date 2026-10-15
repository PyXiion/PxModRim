from __future__ import annotations

from collections.abc import Iterator
from contextlib import contextmanager

from loguru import logger
from ttimer import Timer


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


def adopt(parent: Timer, child: Timer) -> None:
    """Reparent all of *child*'s nodes under *parent*'s current stack position.

    Each root tree in *child* becomes a sibling of the nodes already under
    ``parent._current_node``.  After adoption *child* is discarded (its nodes
    live in *parent* now).
    """
    for stack, node in child._nodes.items():
        parent._nodes[stack] = node
    for root in child.trees:
        root.parent = parent._current_node
