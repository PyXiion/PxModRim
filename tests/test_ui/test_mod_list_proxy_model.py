from __future__ import annotations

from collections.abc import Iterator

import pytest
from PySide6.QtCore import QCoreApplication
from PySide6.QtWidgets import QApplication

from pxmodrim.core.models.metadata.structures import (
    AboutXmlMod,
    CaseInsensitiveStr,
    ListedMod,
)
from pxmodrim.ui.models.mod_list_model import ModListModel
from pxmodrim.ui.models.mod_list_proxy_model import ModListProxyModel


def _provider_colors() -> dict[str, str]:
    return {"stub": "#abcdef"}


def _mod(name: str, pid: str = "") -> AboutXmlMod:
    package_id = CaseInsensitiveStr(pid or name.lower())
    return AboutXmlMod(
        name=name, package_id=package_id, provider_id="stub", valid=True
    )


@pytest.fixture(scope="module")
def qapp() -> Iterator[QApplication]:
    app = QCoreApplication.instance()
    if app is None:
        app = QApplication([])
    assert isinstance(app, QApplication)
    yield app


@pytest.fixture
def source(qapp: QApplication) -> ModListModel:
    colors = _provider_colors()
    m = ModListModel(colors)
    mods: dict[str, ListedMod] = {
        f"uuid-{i}": _mod(f"Mod {i}", f"mod.{i}")
        for i in range(5)
    }
    active = ["uuid-0", "uuid-2"]
    m.load_mods(mods, active)
    return m


@pytest.fixture
def proxy(source: ModListModel) -> ModListProxyModel:
    return ModListProxyModel(source)


class TestSidebarFilter:
    def test_no_filter_shows_all(self, proxy: ModListProxyModel) -> None:
        assert proxy.rowCount() == 5

    def test_filter_to_subset(self, proxy: ModListProxyModel) -> None:
        proxy.set_sidebar_filter({"uuid-0", "uuid-2", "uuid-4"})
        assert proxy.rowCount() == 3

        rows = [
            proxy.mapToSource(proxy.index(i, 0)).row()
            for i in range(proxy.rowCount())
        ]
        # source _items = [uuid-0, uuid-2, uuid-1, uuid-3, uuid-4]
        assert rows == [0, 1, 4]

    def test_filter_to_empty(self, proxy: ModListProxyModel) -> None:
        proxy.set_sidebar_filter(set())
        assert proxy.rowCount() == 0

    def test_filter_reset_to_none(self, proxy: ModListProxyModel) -> None:
        proxy.set_sidebar_filter({"uuid-0"})
        assert proxy.rowCount() == 1
        proxy.set_sidebar_filter(None)
        assert proxy.rowCount() == 5


class TestSearchFilter:
    def test_search_by_name(self, proxy: ModListProxyModel) -> None:
        proxy.set_search_filter("Mod 1")
        assert proxy.rowCount() == 1
        src_row = proxy.mapToSource(proxy.index(0, 0)).row()
        # source _items = [uuid-0, uuid-2, uuid-1, uuid-3, uuid-4]
        assert src_row == 2

    def test_search_by_package_id(self, proxy: ModListProxyModel) -> None:
        proxy.set_search_filter("mod.3")
        assert proxy.rowCount() == 1
        src_row = proxy.mapToSource(proxy.index(0, 0)).row()
        assert src_row == 3

    def test_search_empty_shows_all(self, proxy: ModListProxyModel) -> None:
        proxy.set_search_filter("")
        assert proxy.rowCount() == 5

    def test_search_no_match(self, proxy: ModListProxyModel) -> None:
        proxy.set_search_filter("zzzzzz")
        assert proxy.rowCount() == 0

    def test_search_case_insensitive(self, proxy: ModListProxyModel) -> None:
        proxy.set_search_filter("MOD 2")
        assert proxy.rowCount() == 1


class TestCombinedFilter:
    def test_sidebar_and_search(self, proxy: ModListProxyModel) -> None:
        proxy.set_sidebar_filter({"uuid-1", "uuid-2", "uuid-3"})
        proxy.set_search_filter("Mod 2")
        assert proxy.rowCount() == 1
        src_row = proxy.mapToSource(proxy.index(0, 0)).row()
        # uuid-2 is at source row 1 in [uuid-0, uuid-2, uuid-1, uuid-3, uuid-4]
        assert src_row == 1

    def test_sidebar_excludes_search_match(self, proxy: ModListProxyModel) -> None:
        proxy.set_sidebar_filter({"uuid-0", "uuid-1"})
        proxy.set_search_filter("Mod 3")
        assert proxy.rowCount() == 0

    def test_clearing_search_returns_to_sidebar(self, proxy: ModListProxyModel) -> None:
        proxy.set_sidebar_filter({"uuid-1", "uuid-2"})
        proxy.set_search_filter("Mod 2")
        assert proxy.rowCount() == 1
        proxy.set_search_filter("")
        assert proxy.rowCount() == 2


class TestMapToSource:
    def test_round_trip(self, proxy: ModListProxyModel) -> None:
        proxy.set_sidebar_filter({"uuid-0", "uuid-2", "uuid-4"})
        for proxy_row in range(proxy.rowCount()):
            proxy_index = proxy.index(proxy_row, 0)
            src_index = proxy.mapToSource(proxy_index)
            assert src_index.isValid()
            src_row = src_index.row()
            mapped_back = proxy.mapFromSource(src_index)
            assert mapped_back == proxy_index
