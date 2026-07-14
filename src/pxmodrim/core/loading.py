from __future__ import annotations

from collections.abc import Generator
from contextlib import contextmanager
from dataclasses import dataclass

from PySide6.QtCore import QObject, Signal


@dataclass(slots=True)
class StackFrame:
    status: str
    current_step: int = 0
    total_steps: int = 100


class LoadingState(QObject):
    """Stack-based loading state for a single async operation."""

    changed = Signal(str, int, int, int)  # status, pct, current, total
    finished = Signal()

    def __init__(self, parent: QObject | None = None) -> None:
        """Initialise with an empty stack and no active task."""
        super().__init__(parent)
        self._stack: list[StackFrame] = []

    def push(self, status: str, total_steps: int = 100) -> None:
        """Start a new subtask, pushing a frame onto the loading stack."""
        self._stack.append(StackFrame(status=status, total_steps=total_steps))
        self._emit()

    def pop(self) -> None:
        """Finish the current subtask and restore the parent frame."""
        if self._stack:
            self._stack.pop()
        if not self._stack:
            self.finished.emit()
        else:
            self._emit()

    def step(self, amount: int = 1) -> None:
        """Advance the current task's progress by *amount* steps."""
        if self._stack:
            frame = self._stack[-1]
            frame.current_step = min(frame.total_steps, frame.current_step + amount)
            self._emit()

    def set_total(self, total: int) -> None:
        """Update the total step count for the current task."""
        if self._stack:
            self._stack[-1].total_steps = max(1, total)
            self._emit()

    def set_progress(self, value: int) -> None:
        """Set the current task's progress to an absolute *value*."""
        if self._stack:
            frame = self._stack[-1]
            frame.current_step = max(0, min(frame.total_steps, value))
            self._emit()

    def _emit(self) -> None:
        if not self._stack:
            return
        frame = self._stack[-1]
        pct = (
            int((frame.current_step / frame.total_steps) * 100)
            if frame.total_steps
            else 0
        )
        self.changed.emit(frame.status, pct, frame.current_step, frame.total_steps)

    @contextmanager
    def task(
        self, status: str, total_steps: int = 100
    ) -> Generator[LoadingState, None, None]:
        """Context manager: push a frame on enter, pop on exit (even on exception)."""
        self.push(status, total_steps)
        try:
            yield self
        finally:
            self.pop()
