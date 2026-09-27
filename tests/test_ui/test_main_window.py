from __future__ import annotations

from typing import cast

from pxmodrim.ui.window.main_window import MainWindow


def test_view_shortcuts_wrap_and_ignore_invalid_indexes() -> None:
    selections: list[int] = []

    class Stack:
        index = 0

        def count(self) -> int:
            return 3

        def currentIndex(self) -> int:
            return self.index

    stack = Stack()

    class Rail:
        def set_current(self, index: int) -> None:
            selections.append(index)
            stack.index = index

    class Window:
        _stack = stack
        _rail = Rail()

        def _select_view(self, index: int) -> None:
            MainWindow._select_view(cast(MainWindow, self), index)

    window = cast(MainWindow, Window())
    MainWindow._cycle_view(window, -1)
    assert selections == [2]
    MainWindow._cycle_view(window, 1)
    assert selections == [2, 0]
    MainWindow._select_view(window, 3)
    assert selections == [2, 0]
