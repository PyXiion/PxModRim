from __future__ import annotations

from collections.abc import Iterator

import pytest
from PySide6.QtCore import QCoreApplication
from PySide6.QtWidgets import QApplication, QLabel, QVBoxLayout, QWidget

from pxmodrim.ui.components.accordion_section import AccordionSection


@pytest.fixture(scope="module")
def qapp() -> Iterator[QApplication]:
    app = QCoreApplication.instance()
    if app is None:
        app = QApplication([])
    assert isinstance(app, QApplication)
    yield app


@pytest.fixture
def window(qapp: QApplication) -> Iterator[tuple[QWidget, QVBoxLayout]]:
    w = QWidget()
    layout = QVBoxLayout(w)
    w.show()
    qapp.processEvents()
    yield (w, layout)
    w.hide()
    qapp.processEvents()


def _qapp() -> QApplication:
    app = QCoreApplication.instance()
    assert isinstance(app, QApplication)
    return app


class TestAccordionSection:
    def test_initial_collapsed(self, window: tuple[QWidget, QVBoxLayout]) -> None:
        _w, layout = window
        label = QLabel("test")
        accordion = AccordionSection("Title", label, _animation_duration=0)
        layout.addWidget(accordion)
        _qapp().processEvents()
        assert not accordion.is_expanded()
        assert not label.isVisible()

    def test_initial_expanded(self, window: tuple[QWidget, QVBoxLayout]) -> None:
        _w, layout = window
        label = QLabel("test")
        accordion = AccordionSection(
            "Title", label, expanded=True, _animation_duration=0
        )
        layout.addWidget(accordion)
        _qapp().processEvents()
        assert accordion.is_expanded()
        assert label.isVisible()
        assert accordion.sizeHint().height() > 0

    def test_toggle(self, window: tuple[QWidget, QVBoxLayout]) -> None:
        _w, layout = window
        label = QLabel("test")
        accordion = AccordionSection("Title", label, _animation_duration=0)
        layout.addWidget(accordion)
        _qapp().processEvents()
        assert not accordion.is_expanded()

        signals: list[bool] = []
        accordion.toggled.connect(signals.append)

        h_collapsed = accordion.sizeHint().height()
        assert h_collapsed > 0

        accordion.toggle()
        assert accordion.is_expanded()
        assert label.isVisible()
        assert len(signals) == 1 and signals[0] is True
        assert accordion.sizeHint().height() > h_collapsed

        accordion.toggle()
        assert not accordion.is_expanded()
        assert not label.isVisible()
        assert len(signals) == 2 and signals[1] is False
        assert accordion.sizeHint().height() == h_collapsed

    def test_set_expanded_noop(self, window: tuple[QWidget, QVBoxLayout]) -> None:
        _w, layout = window
        label = QLabel("test")
        accordion = AccordionSection(
            "Title", label, expanded=True, _animation_duration=0
        )
        layout.addWidget(accordion)
        _qapp().processEvents()
        signals: list[bool] = []
        accordion.toggled.connect(signals.append)
        accordion.set_expanded(True)
        assert len(signals) == 0

    def test_title(self, window: tuple[QWidget, QVBoxLayout]) -> None:
        _w, layout = window
        accordion = AccordionSection("Initial", QLabel("x"), _animation_duration=0)
        layout.addWidget(accordion)
        _qapp().processEvents()
        assert accordion.title_text() == "Initial"
        accordion.set_title("Updated")
        assert accordion.title_text() == "Updated"

    def test_content_resize_while_expanded(
        self, window: tuple[QWidget, QVBoxLayout]
    ) -> None:
        _w, layout = window
        label = QLabel("line1")
        accordion = AccordionSection(
            "Title", label, expanded=True, _animation_duration=0
        )
        layout.addWidget(accordion)
        _qapp().processEvents()
        h_before = accordion.sizeHint().height()

        label.setText("line1\nline2\nline3")
        _qapp().processEvents()
        h_after = accordion.sizeHint().height()
        assert h_after > h_before
