from __future__ import annotations

from collections.abc import Iterator

import pytest
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QPushButton, QWidget

from pxmodrim.core.loading import LoadingState
from pxmodrim.ui.components.progress_dialog import ProgressDialog
from pxmodrim.ui.components.toast import ToastManager


@pytest.fixture
def qapp() -> Iterator[QApplication]:
    app = QApplication.instance()
    if app is None:
        app = QApplication([])
    assert isinstance(app, QApplication)
    yield app


def test_close_button_removes_persistent_toast(qapp: QApplication) -> None:
    parent = QWidget()
    manager = ToastManager(parent)
    manager.show_toast("stays", "info", duration_ms=0)
    toast = manager._active_toasts[0]

    close_button = toast.findChild(QPushButton, "toastClose")
    assert close_button is not None
    close_button.click()
    QTest.qWait(300)

    assert manager._active_toasts == []
    assert manager._layout.count() == 0


def test_progress_dialog_stays_visible_when_new_task_starts_before_hide(
    qapp: QApplication,
) -> None:
    parent = QWidget()
    loading = LoadingState(parent)
    dialog = ProgressDialog(loading, parent)

    with loading.task("first"):
        pass
    with loading.task("second"):
        QTest.qWait(500)
        assert dialog.isVisible()
        assert dialog.windowTitle() == "second"

    QTest.qWait(800)
    assert not dialog.isVisible()
