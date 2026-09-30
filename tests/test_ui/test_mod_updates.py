from __future__ import annotations

from collections.abc import Iterator

import pytest
from PySide6.QtWidgets import QApplication, QMessageBox, QWidget

from pxmodrim.ui.components import mod_updates


@pytest.fixture(scope="module")
def qapp() -> Iterator[QApplication]:
    app = QApplication.instance() or QApplication([])
    assert isinstance(app, QApplication)
    yield app


@pytest.mark.asyncio
async def test_small_update_skips_confirmation(
    qapp: QApplication,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    async def fail(*_args: object) -> tuple[int, object]:
        raise AssertionError("dialog must not open")

    monkeypatch.setattr(mod_updates, "await_dialog", fail)
    parent = QWidget()
    assert await mod_updates.confirm_update(mod_updates.CONFIRM_THRESHOLD - 1, parent)


@pytest.mark.asyncio
@pytest.mark.parametrize(
    ("answer", "expected"),
    [
        (QMessageBox.StandardButton.Yes, True),
        (QMessageBox.StandardButton.Cancel, False),
    ],
)
async def test_large_update_follows_dialog_answer(
    qapp: QApplication,
    monkeypatch: pytest.MonkeyPatch,
    answer: QMessageBox.StandardButton,
    expected: bool,
) -> None:
    async def fake(*_args: object) -> tuple[int, object]:
        return answer, object()

    monkeypatch.setattr(mod_updates, "await_dialog", fake)
    parent = QWidget()
    result = await mod_updates.confirm_update(mod_updates.CONFIRM_THRESHOLD, parent)
    assert result is expected
