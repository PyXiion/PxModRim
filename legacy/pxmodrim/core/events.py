from __future__ import annotations

from collections.abc import Callable
from typing import Any


class Event[T]:
    __slots__ = ("_handlers",)

    def __init__(self) -> None:
        self._handlers: list[Callable[[T], Any]] = []

    def connect(self, handler: Callable[[T], Any]) -> None:
        if handler not in self._handlers:
            self._handlers.append(handler)

    def disconnect(self, handler: Callable[[T], Any]) -> None:
        self._handlers.remove(handler)

    def emit(self, data: T) -> None:
        for handler in self._handlers:
            handler(data)
