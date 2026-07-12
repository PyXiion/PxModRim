from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from PySide6.QtCore import (
    QAbstractListModel,
    QByteArray,
    QModelIndex,
    QPersistentModelIndex,
    QSortFilterProxyModel,
    Qt,
    Signal,
)

from pxmodrim.models.metadata.structures import AboutXmlMod, ListedMod


@dataclass
class ModItem:
    mod: ListedMod
    uuid: str
    checked: bool = False


class ModListModel(QAbstractListModel):
    # Custom roles
    ModRole = Qt.ItemDataRole.UserRole + 1
    CheckStateRole = Qt.ItemDataRole.UserRole + 2
    UuidRole = Qt.ItemDataRole.UserRole + 3

    active_mods_changed = Signal()

    def __init__(self) -> None:
        super().__init__()
        self._items: list[ModItem] = []

    def rowCount(self, parent: QModelIndex | QPersistentModelIndex = QModelIndex()) -> int:
        if parent.isValid():
            return 0
        return len(self._items)

    def data(
        self, index: QModelIndex | QPersistentModelIndex, role: int = Qt.ItemDataRole.DisplayRole
    ) -> Any:
        if not index.isValid() or index.row() >= len(self._items):
            return None

        item = self._items[index.row()]

        if role == Qt.ItemDataRole.DisplayRole:
            return item.mod.name
        elif role == self.ModRole:
            return item.mod
        elif role == self.CheckStateRole:
            return Qt.CheckState.Checked if item.checked else Qt.CheckState.Unchecked
        elif role == self.UuidRole:
            return item.uuid
        elif role == Qt.ItemDataRole.ToolTipRole:
            if hasattr(item.mod, "description") and item.mod.description:
                return item.mod.description

        return None

    def setData(
        self,
        index: QModelIndex | QPersistentModelIndex,
        value: Any,
        role: int = Qt.ItemDataRole.EditRole,
    ) -> bool:
        if not index.isValid() or index.row() >= len(self._items):
            return False

        if role == self.CheckStateRole:
            item = self._items[index.row()]
            new_checked = value == Qt.CheckState.Checked
            if item.checked != new_checked:
                item.checked = new_checked
                self.dataChanged.emit(index, index, [self.CheckStateRole])
                self.active_mods_changed.emit()
            return True

        return False

    def flags(self, index: QModelIndex | QPersistentModelIndex) -> Qt.ItemFlag:
        if not index.isValid():
            return Qt.ItemFlag.NoItemFlags

        return (
            Qt.ItemFlag.ItemIsEnabled
            | Qt.ItemFlag.ItemIsSelectable
        )

    def roleNames(self) -> dict[int, QByteArray]:
        return {
            Qt.ItemDataRole.DisplayRole: QByteArray(b"name"),
            self.ModRole: QByteArray(b"mod"),
            self.CheckStateRole: QByteArray(b"checkState"),
            self.UuidRole: QByteArray(b"uuid"),
        }

    def load_mods(
        self, mods: dict[str, ListedMod], active_uuids: set[str]
    ) -> None:
        self.beginResetModel()

        sorted_uuids = [
            uuid
            for uuid, _ in sorted(
                mods.items(),
                key=lambda kv: kv[1].name.lower(),
            )
        ]

        self._items = [
            ModItem(
                mod=mods[uuid],
                uuid=uuid,
                checked=uuid in active_uuids,
            )
            for uuid in sorted_uuids
        ]

        self.endResetModel()

    def active_uuids(self) -> list[str]:
        return [item.uuid for item in self._items if item.checked]

    def get_item_by_uuid(self, uuid: str) -> ModItem | None:
        for item in self._items:
            if item.uuid == uuid:
                return item
        return None

    def get_item(self, row: int) -> ModItem | None:
        if 0 <= row < len(self._items):
            return self._items[row]
        return None


class ModFilterProxyModel(QSortFilterProxyModel):
    def __init__(self, source_model: ModListModel) -> None:
        super().__init__()
        self.setSourceModel(source_model)
        self.setDynamicSortFilter(True)
        self._search_text = ""
        self._sidebar_uuids: set[str] | None = None

    def set_search_filter(self, search_text: str) -> None:
        self._search_text = search_text.lower()
        self.invalidateFilter()

    def set_sidebar_filter(self, uuids: set[str] | None) -> None:
        self._sidebar_uuids = uuids
        self.invalidateFilter()

    def filterAcceptsRow(
        self, source_row: int, source_parent: QModelIndex | QPersistentModelIndex
    ) -> bool:
        index = self.sourceModel().index(source_row, 0, source_parent)
        if not index.isValid():
            return False

        source = self.sourceModel()
        if not isinstance(source, ModListModel):
            return True

        item = source.get_item(source_row)
        if item is None:
            return False

        # Check sidebar filter first
        if self._sidebar_uuids is not None and item.uuid not in self._sidebar_uuids:
            return False

        # Check search filter
        if self._search_text:
            name_match = self._search_text in item.mod.name.lower()
            pid_match = (
                isinstance(item.mod, AboutXmlMod)
                and self._search_text in str(item.mod.package_id).lower()
            )
            return name_match or pid_match

        return True

    def lessThan(
        self,
        source_left: QModelIndex | QPersistentModelIndex,
        source_right: QModelIndex | QPersistentModelIndex,
    ) -> bool:
        left_data = source_left.data(Qt.ItemDataRole.DisplayRole)
        right_data = source_right.data(Qt.ItemDataRole.DisplayRole)
        if isinstance(left_data, str) and isinstance(right_data, str):
            return left_data.lower() < right_data.lower()
        return super().lessThan(source_left, source_right)