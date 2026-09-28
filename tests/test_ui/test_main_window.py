from __future__ import annotations

from types import SimpleNamespace
from typing import cast

import pytest
from PySide6.QtGui import QCloseEvent
from PySide6.QtWidgets import QMessageBox

from pxmodrim.ui.window import main_window as main_window_module
from pxmodrim.ui.window.main_window import MainWindow, UnsavedChangesDialog


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


def test_dirty_state_tracks_active_list_order_and_updates_header() -> None:
    class Header:
        unsaved_changes = False

        def set_unsaved_changes(self, value: bool) -> None:
            self.unsaved_changes = value

    header = Header()
    context = SimpleNamespace(active_uuids=["uuid-a", "uuid-b"])
    window = cast(
        MainWindow,
        SimpleNamespace(
            _ctx=context,
            _saved_active_uuids=["uuid-a", "uuid-b"],
            _unsaved_changes=False,
            _header_controller=header,
            setWindowTitle=lambda _title: None,
        ),
    )

    context.active_uuids.reverse()
    MainWindow._refresh_unsaved_state(window)
    assert header.unsaved_changes is True

    context.active_uuids.reverse()
    MainWindow._refresh_unsaved_state(window)
    assert header.unsaved_changes is False


@pytest.mark.asyncio
async def test_successful_save_clears_unsaved_state() -> None:
    class ModService:
        saved: list[str] | None = None

        async def save_active_layout(self, active_uuids: list[str]) -> bool:
            self.saved = active_uuids
            return True

    class Header:
        unsaved_changes = True

        def set_unsaved_changes(self, value: bool) -> None:
            self.unsaved_changes = value

    class Window(SimpleNamespace):
        def _refresh_unsaved_state(self) -> None:
            MainWindow._refresh_unsaved_state(cast(MainWindow, self))

    mod_service = ModService()
    header = Header()
    context = SimpleNamespace(
        active_uuids=["uuid-b", "uuid-a"], mod_service=mod_service
    )
    window = cast(
        MainWindow,
        Window(
            _ctx=context,
            _saved_active_uuids=["uuid-a", "uuid-b"],
            _unsaved_changes=True,
            _header_controller=header,
            setWindowTitle=lambda _title: None,
            _toast_manager=SimpleNamespace(
                success=lambda *_args: None, warning=lambda *_args: None
            ),
            mod_list=SimpleNamespace(active_uuids=lambda: ["uuid-b", "uuid-a"]),
        ),
    )

    assert await MainWindow._save_active_mods(window) is True
    assert mod_service.saved == ["uuid-b", "uuid-a"]
    assert window._saved_active_uuids == ["uuid-b", "uuid-a"]
    assert header.unsaved_changes is False


@pytest.mark.asyncio
@pytest.mark.parametrize(
    ("choice", "save_succeeds", "expected_closed"),
    [
        (QMessageBox.StandardButton.Save, True, True),
        (QMessageBox.StandardButton.Save, False, False),
        (QMessageBox.StandardButton.Discard, True, True),
        (QMessageBox.StandardButton.Cancel, True, False),
    ],
)
async def test_async_close_choice(
    monkeypatch: pytest.MonkeyPatch,
    choice: QMessageBox.StandardButton,
    save_succeeds: bool,
    expected_closed: bool,
) -> None:
    async def choose(
        dialog: type[QMessageBox], _parent: object
    ) -> tuple[QMessageBox.StandardButton, None]:
        assert dialog is UnsavedChangesDialog
        return choice, None

    monkeypatch.setattr(main_window_module, "await_dialog", choose)

    class Window:
        _close_prompt_open = True
        _close_confirmed = False
        closed = False
        save_calls = 0

        async def _save_active_mods(self) -> bool:
            self.save_calls += 1
            return save_succeeds

        def close(self) -> None:
            self.closed = True

    window = Window()
    await MainWindow._confirm_close(cast(MainWindow, window))

    assert window.closed is expected_closed
    assert window._close_confirmed is expected_closed
    assert window.save_calls == (choice == QMessageBox.StandardButton.Save)


def test_dirty_close_event_is_ignored_until_confirmation() -> None:
    class Event:
        ignored = False

        def ignore(self) -> None:
            self.ignored = True

    window = cast(
        MainWindow,
        SimpleNamespace(
            _unsaved_changes=True,
            _close_confirmed=False,
            _close_prompt_open=True,
        ),
    )
    event = Event()

    MainWindow.closeEvent(window, cast(QCloseEvent, event))
    assert event.ignored is True
