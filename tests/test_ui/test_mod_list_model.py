from __future__ import annotations

from collections.abc import Iterator

import pytest
from PySide6.QtCore import QCoreApplication, Qt
from PySide6.QtWidgets import QApplication

from pxmodrim.core.models.metadata.structures import AboutXmlMod, ListedMod
from pxmodrim.ui.models.mod_list_model import ModListModel


def _provider_colors() -> dict[str, str]:
    return {"stub": "#abcdef"}


def _mod(name: str) -> AboutXmlMod:
    return AboutXmlMod(name=name, provider_id="stub", valid=True)


@pytest.fixture(scope="module")
def qapp() -> Iterator[QApplication]:
    app = QCoreApplication.instance()
    if app is None:
        app = QApplication([])
    assert isinstance(app, QApplication)
    yield app


@pytest.fixture
def model(qapp: QApplication) -> ModListModel:
    colors = _provider_colors()
    m = ModListModel(colors)
    mods: dict[str, ListedMod] = {f"uuid-{i}": _mod(f"Mod {i}") for i in range(5)}
    active = ["uuid-0", "uuid-2"]
    m.load_mods(mods, active)
    return m


class TestSetData:
    def test_toggle_check_emits_dataChanged_not_reset(
        self, model: ModListModel
    ) -> None:
        resets: list[None] = []
        model.modelReset.connect(lambda: resets.append(None))
        data_changes: list[None] = []
        model.dataChanged.connect(lambda *_: data_changes.append(None))

        inactive_rows = [i for i, it in enumerate(model._items) if not it.checked]
        idx = model.index(inactive_rows[0], 0)
        ok = model.setData(idx, Qt.CheckState.Checked, ModListModel.CheckStateRole)
        assert ok is True
        item = model.get_item(inactive_rows[0])
        assert item is not None
        assert item.checked is True
        assert data_changes
        assert resets == []

    def test_toggle_unchanged_is_noop(self, model: ModListModel) -> None:
        active_rows = [i for i, it in enumerate(model._items) if it.checked]
        idx = model.index(active_rows[0], 0)
        data_changes: list[None] = []
        model.dataChanged.connect(lambda *_: data_changes.append(None))
        ok = model.setData(idx, Qt.CheckState.Checked, ModListModel.CheckStateRole)
        assert ok is True
        assert data_changes == []


class TestCommitOrder:
    def test_reorder_subset_preserves_positions(self, model: ModListModel) -> None:
        before = [it.uuid for it in model._items]
        # _items = [uuid-0, uuid-2, uuid-1, uuid-3, uuid-4]
        # reorder active mods uuid-0 and uuid-2 within their positions
        model.commitOrder(["uuid-2", "uuid-0"])
        after = [it.uuid for it in model._items]
        # uuid-2 swaps into uuid-0's position (0), uuid-0 goes to uuid-2's position (1)
        assert after == ["uuid-2", "uuid-0", "uuid-1", "uuid-3", "uuid-4"]
        assert set(after) == set(before)

    def test_reorder_full_list(self, model: ModListModel) -> None:
        before = [it.uuid for it in model._items]
        target = list(reversed(before))
        model.commitOrder(target)
        assert [it.uuid for it in model._items] == target

    def test_noop_on_same_order(self, model: ModListModel) -> None:
        layout_changes: list[None] = []
        model.layoutChanged.connect(lambda *_: layout_changes.append(None))
        model.commitOrder([it.uuid for it in model._items])
        assert layout_changes == []


class TestActiveUuids:
    def test_returns_only_checked_in_order(self, model: ModListModel) -> None:
        active = model.active_uuids()
        assert set(active) == {"uuid-0", "uuid-2"}
        assert active == ["uuid-0", "uuid-2"]
