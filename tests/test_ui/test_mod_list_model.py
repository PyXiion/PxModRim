from __future__ import annotations

from collections.abc import Iterator

import pytest
from PySide6.QtCore import QCoreApplication, QPersistentModelIndex, Qt
from PySide6.QtWidgets import QApplication

from pxmodrim.core.models.metadata.structures import (
    AboutXmlMod,
    CaseInsensitiveStr,
    ListedMod,
)
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


class TestLoadMods:
    def test_active_uuid_missing_from_mods_is_skipped(self, qapp: QApplication) -> None:
        m = ModListModel(_provider_colors())
        m.load_mods({"a": _mod("A")}, ["ghost", "a"])
        assert m.active_uuids() == ["a"]


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

    def test_layout_signals_wrap_reorder_and_preserve_persistent_index(
        self, model: ModListModel
    ) -> None:
        before = [item.uuid for item in model._items]
        target = list(reversed(before))
        persistent_index = QPersistentModelIndex(model.index(0, 0))
        uuid = persistent_index.data(ModListModel.UuidRole)
        events: list[tuple[str, list[str]]] = []

        model.layoutAboutToBeChanged.connect(
            lambda *_: events.append(
                ("about-to-change", [item.uuid for item in model._items])
            )
        )
        model.layoutChanged.connect(
            lambda *_: events.append(("changed", [item.uuid for item in model._items]))
        )

        model.commitOrder(target)

        assert events == [("about-to-change", before), ("changed", target)]
        assert persistent_index.data(ModListModel.UuidRole) == uuid


class TestActiveUuids:
    def test_returns_only_checked_in_order(self, model: ModListModel) -> None:
        active = model.active_uuids()
        assert set(active) == {"uuid-0", "uuid-2"}
        assert active == ["uuid-0", "uuid-2"]


class TestLoadIndex:
    def test_initial_load_indices(self, model: ModListModel) -> None:
        indices = [
            model.data(model.index(r, 0), ModListModel.LoadIndexRole)
            for r in range(model.rowCount())
        ]
        assert indices == [1, 2, -1, -1, -1]

    def test_load_index_after_reorder(self, model: ModListModel) -> None:
        changed_roles: list[list[int]] = []
        model.dataChanged.connect(lambda _t, _b, roles: changed_roles.append(roles))

        ok = model.move_row(0, 1)
        assert ok is True

        indices = [
            model.data(model.index(r, 0), ModListModel.LoadIndexRole)
            for r in range(model.rowCount())
        ]
        assert indices == [1, 2, -1, -1, -1]
        assert model.data(model.index(0, 0), ModListModel.UuidRole) == "uuid-2"
        assert model.data(model.index(1, 0), ModListModel.UuidRole) == "uuid-0"
        assert any(ModListModel.LoadIndexRole in roles for roles in changed_roles)

    def test_load_index_after_toggle(self, model: ModListModel) -> None:
        changed_roles: list[list[int]] = []
        model.dataChanged.connect(lambda _t, _b, roles: changed_roles.append(roles))

        idx2 = model.index(2, 0)
        assert model.data(idx2, ModListModel.LoadIndexRole) == -1
        model.setData(idx2, Qt.CheckState.Checked, ModListModel.CheckStateRole)

        assert model.data(idx2, ModListModel.LoadIndexRole) == 3
        assert any(ModListModel.LoadIndexRole in roles for roles in changed_roles)

        changed_roles.clear()
        idx0 = model.index(0, 0)
        model.setData(idx0, Qt.CheckState.Unchecked, ModListModel.CheckStateRole)

        assert model.data(idx0, ModListModel.LoadIndexRole) == -1
        assert model.data(model.index(1, 0), ModListModel.LoadIndexRole) == 1
        assert model.data(model.index(2, 0), ModListModel.LoadIndexRole) == 2
        assert any(ModListModel.LoadIndexRole in roles for roles in changed_roles)


class TestProviderLabel:
    def test_provider_label_mapping(self, qapp: QApplication) -> None:
        m = ModListModel({})
        mods: dict[str, ListedMod] = {
            "uuid-steam": AboutXmlMod(
                name="Workshop Mod", provider_id="steam", valid=True
            ),
            "uuid-downloaded": AboutXmlMod(
                name="Downloaded Mod", provider_id="downloaded", valid=True
            ),
            "uuid-local": AboutXmlMod(
                name="Local Mod", provider_id="local", valid=True
            ),
            "uuid-core": AboutXmlMod(
                name="Core",
                package_id=CaseInsensitiveStr("ludeon.rimworld"),
                provider_id="core",
                valid=True,
            ),
            "uuid-dlc": AboutXmlMod(
                name="Royalty",
                package_id=CaseInsensitiveStr("ludeon.rimworld.royalty"),
                provider_id="core",
                valid=True,
            ),
        }
        m.load_mods(mods, list(mods.keys()))

        labels = {
            m.data(m.index(r, 0), ModListModel.UuidRole): m.data(
                m.index(r, 0), ModListModel.ProviderLabelRole
            )
            for r in range(m.rowCount())
        }
        assert labels["uuid-steam"] == "Workshop"
        assert labels["uuid-downloaded"] == "Workshop"
        assert labels["uuid-local"] == "Local"
        assert labels["uuid-core"] == "Core"
        assert labels["uuid-dlc"] == "DLC"


def test_setting_startup_impact_on_empty_model_emits_no_data_changed(
    qapp: QApplication,
) -> None:
    model = ModListModel(_provider_colors())
    changes: list[None] = []
    model.dataChanged.connect(lambda *_: changes.append(None))

    model.set_startup_impact({"example.mod": 1.0})

    assert changes == []
