from __future__ import annotations

import asyncio
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

    class Window:
        _stack = stack
        _rail = Rail()

        def _select_view(self, index: int) -> None:
            MainWindow._select_view(cast(MainWindow, self), index)

        def _on_rail_tab_changed(self, index: int) -> None:
            stack.index = index

    window = cast(MainWindow, Window())
    MainWindow._cycle_view(window, -1)
    assert selections == [2]
    assert stack.index == 2
    MainWindow._cycle_view(window, 1)
    assert selections == [2, 0]
    assert stack.index == 0
    MainWindow._select_view(window, 3)
    assert selections == [2, 0]


def test_route_selects_the_view_then_hands_it_the_path() -> None:
    opened: list[tuple[str, ...]] = []
    selected: list[int] = []

    def view(view_id: str) -> SimpleNamespace:
        return SimpleNamespace(
            view_id=view_id,
            open_route=lambda path, v=view_id: opened.append((v, *path)),
        )

    window = cast(
        MainWindow,
        SimpleNamespace(
            _views=[view("mods"), view("workshop")], _select_view=selected.append
        ),
    )
    MainWindow._open_route(window, "pxmodrim://workshop/mod/42")
    MainWindow._open_route(window, "pxmodrim://missing/x")
    MainWindow._open_route(window, "workshop")
    assert selected == [1]
    assert opened == [("workshop", "mod", "42")]


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
            setWindowModified=lambda _modified: None,
        ),
    )

    context.active_uuids.reverse()
    MainWindow._refresh_unsaved_state(window)
    assert header.unsaved_changes is True

    context.active_uuids.reverse()
    MainWindow._refresh_unsaved_state(window)
    assert header.unsaved_changes is False


def test_reload_baseline_is_list_loaded_from_disk_not_later_edits() -> None:
    class Header:
        unsaved_changes = False

        def set_unsaved_changes(self, value: bool) -> None:
            self.unsaved_changes = value

    class Window(SimpleNamespace):
        def _refresh_unsaved_state(self) -> None:
            MainWindow._refresh_unsaved_state(cast(MainWindow, self))

    header = Header()
    # The user deactivated uuid-b after ctx.load() but before mods_changed fired.
    context = SimpleNamespace(
        active_uuids=["uuid-a"], loaded_active_uuids=["uuid-a", "uuid-b"]
    )
    window = cast(
        MainWindow,
        Window(
            _ctx=context,
            _saved_active_uuids=[],
            _unsaved_changes=False,
            _header_controller=header,
            setWindowModified=lambda _modified: None,
        ),
    )

    MainWindow._on_mods_reloaded(window)

    assert header.unsaved_changes is True


@pytest.mark.asyncio
async def test_successful_save_clears_unsaved_state() -> None:
    class ModService:
        async def save_active_layout(self, _active_uuids: list[str]) -> bool:
            return True

    class Header:
        unsaved_changes = True

        def set_unsaved_changes(self, value: bool) -> None:
            self.unsaved_changes = value

    class Window(SimpleNamespace):
        def _refresh_unsaved_state(self) -> None:
            MainWindow._refresh_unsaved_state(cast(MainWindow, self))

        async def _write_active_layout(self) -> list[str] | None:
            return await MainWindow._write_active_layout(cast(MainWindow, self))

    header = Header()
    context = SimpleNamespace(
        active_uuids=["uuid-b", "uuid-a"], mod_service=ModService()
    )
    window = cast(
        MainWindow,
        Window(
            _ctx=context,
            _saved_active_uuids=["uuid-a", "uuid-b"],
            _unsaved_changes=True,
            _header_controller=header,
            setWindowModified=lambda _modified: None,
            _toast_manager=SimpleNamespace(
                success=lambda *_args: None, warning=lambda *_args: None
            ),
            mod_list=SimpleNamespace(active_uuids=lambda: ["uuid-b", "uuid-a"]),
        ),
    )

    assert await MainWindow._save_active_mods(window) is True
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


class _CloseEvent:
    ignored = False
    accepted = False

    def ignore(self) -> None:
        self.ignored = True

    def accept(self) -> None:
        self.accepted = True


@pytest.mark.asyncio
async def test_dirty_close_event_is_ignored_and_prompts_once() -> None:
    prompts = 0

    class Window:
        _unsaved_changes = True
        _close_confirmed = False
        _close_prompt_open = False
        _close_task: asyncio.Task[None] | None = None

        async def _confirm_close(self) -> None:
            nonlocal prompts
            prompts += 1

    window = Window()
    first, second = _CloseEvent(), _CloseEvent()

    MainWindow.closeEvent(cast(MainWindow, window), cast(QCloseEvent, first))
    MainWindow.closeEvent(cast(MainWindow, window), cast(QCloseEvent, second))
    assert window._close_task is not None
    await window._close_task

    assert first.ignored and second.ignored
    assert not first.accepted
    assert prompts == 1


def test_confirmed_dirty_close_event_is_accepted(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(
        main_window_module, "QApplication", SimpleNamespace(instance=lambda: None)
    )
    quit_calls: list[None] = []
    window = SimpleNamespace(
        _unsaved_changes=True,
        _close_confirmed=True,
        _views=[],
        _update_task=None,
        _launch_task=None,
        _app_quit_callback=lambda: quit_calls.append(None),
        deleteLater=lambda: None,
    )
    event = _CloseEvent()

    MainWindow.closeEvent(cast(MainWindow, window), cast(QCloseEvent, event))

    assert event.accepted and not event.ignored
    assert quit_calls == [None]
