from __future__ import annotations

from collections.abc import Generator
from contextlib import contextmanager
from typing import Protocol, Self


class ProgressReporter(Protocol):
    """Minimal progress-reporting interface consumed by sort-layer operations."""

    @contextmanager
    def task(
        self, status: str, total_steps: int = 100
    ) -> Generator[Self, None, None]: ...

    def step(self, amount: int = 1) -> None: ...
