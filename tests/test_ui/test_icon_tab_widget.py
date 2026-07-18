from __future__ import annotations

from collections.abc import Iterator

import pytest
from PySide6.QtCore import QCoreApplication
from PySide6.QtWidgets import QApplication, QLabel, QVBoxLayout, QWidget

from pxmodrim.ui.components.icon_tab_widget import IconTabWidget, _TabButton


@pytest.fixture(scope="module")
def qapp() -> Iterator[QApplication]:
    app = QCoreApplication.instance()
    if app is None:
        app = QApplication([])
    assert isinstance(app, QApplication)
    yield app


@pytest.fixture
def host(qapp: QApplication) -> Iterator[tuple[QWidget, QVBoxLayout]]:
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


class TestIconTabWidget:
    def test_add_tab_count_and_switch(
        self, host: tuple[QWidget, QVBoxLayout]
    ) -> None:
        _w, layout = host
        tabs = IconTabWidget(orientation="vertical")
        layout.addWidget(tabs)

        a = QLabel("a")
        b = QLabel("b")
        tabs.addTab(a, "grid", "Mods")
        tabs.addTab(b, "clock", "Analytics")

        _qapp().processEvents()
        assert tabs.count() == 2
        assert tabs.currentIndex() == 0
        assert tabs.widget(0) is a

        signals: list[int] = []
        tabs.currentChanged.connect(signals.append)
        tabs.setCurrentIndex(1)
        assert tabs.currentIndex() == 1
        assert tabs.widget(1) is b
        assert signals == [1]

    def test_vertical_collapse_hides_label(
        self, host: tuple[QWidget, QVBoxLayout]
    ) -> None:
        _w, layout = host
        tabs = IconTabWidget(orientation="vertical")
        layout.addWidget(tabs)
        tabs.addTab(QLabel("a"), "grid", "Mods")
        _qapp().processEvents()

        btn = tabs._buttons[0]
        assert isinstance(btn, _TabButton)

        btn.set_collapsed(True)
        assert btn._collapsed
        assert btn.text() == ""
        assert btn.toolTip() == "Mods"

        btn.set_collapsed(False)
        assert not btn._collapsed
        assert btn.text() == "Mods"
        assert btn.toolTip() == ""

    def test_update_collapsed_below_threshold(
        self, host: tuple[QWidget, QVBoxLayout]
    ) -> None:
        _w, layout = host
        tabs = IconTabWidget(orientation="vertical")
        layout.addWidget(tabs)
        tabs.addTab(QLabel("a"), "grid", "Mods")
        _qapp().processEvents()

        tabs._tab_bar.setFixedWidth(48)
        tabs.update_collapsed()
        assert tabs._buttons[0]._collapsed is True

        tabs._tab_bar.setFixedWidth(160)
        tabs.update_collapsed()
        assert tabs._buttons[0]._collapsed is False

    def test_update_collapsed_noop_for_horizontal(
        self, host: tuple[QWidget, QVBoxLayout]
    ) -> None:
        _w, layout = host
        tabs = IconTabWidget(orientation="horizontal")
        layout.addWidget(tabs)
        tabs.addTab(QLabel("a"), "grid", "Mods")
        _qapp().processEvents()

        tabs._buttons[0].set_collapsed(True)
        assert tabs._buttons[0]._collapsed is True
        tabs.update_collapsed()
        assert tabs._buttons[0]._collapsed is True
