from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any, Sequence

from PySide6.QtCore import (
    QAbstractListModel,
    QByteArray,
    QMimeData,
    QModelIndex,
    QPersistentModelIndex,
    Qt,
    QTimer,
    Signal,
)

from pxmodrim.models.metadata.structures import AboutXmlMod, ListedMod


@dataclass
class ModItem:
    mod: ListedMod
    uuid: str
    checked: bool = False


class ModListModel(QAbstractListModel):
    ModRole = Qt.ItemDataRole.UserRole + 1
    CheckStateRole = Qt.ItemDataRole.UserRole + 2
    UuidRole = Qt.ItemDataRole.UserRole + 3

    MIME_TYPE = "application/x-pxmodrim-mod-index"
    active_mods_changed = Signal()

    def __init__(self) -> None:
        super().__init__()
        self._all_items: list[ModItem] = []
        self._visible_items: list[ModItem] = []

        self._search_text = ""
        self._sidebar_uuids: set[str] | None = None

    def set_search_filter(self, search_text: str) -> None:
        self._search_text = search_text.lower()
        self.apply_filter()

    def set_sidebar_filter(self, uuids: set[str] | None) -> None:
        self._sidebar_uuids = uuids
        self.apply_filter()

    def apply_filter(self) -> None:
        self.beginResetModel()
        self._visible_items = []

        for item in self._all_items:
            if self._sidebar_uuids is not None and item.uuid not in self._sidebar_uuids:
                continue

            if self._search_text:
                name_match = self._search_text in item.mod.name.lower()
                pid_match = (
                    isinstance(item.mod, AboutXmlMod)
                    and self._search_text in str(item.mod.package_id).lower()
                )
                if not (name_match or pid_match):
                    continue

            self._visible_items.append(item)

        self.endResetModel()

    def rowCount(
        self, parent: QModelIndex | QPersistentModelIndex = QModelIndex()
    ) -> int:
        if parent.isValid():
            return 0
        return len(self._visible_items)

    def data(
        self,
        index: QModelIndex | QPersistentModelIndex,
        role: int = Qt.ItemDataRole.DisplayRole,
    ) -> Any:
        if (
            not index.isValid()
            or index.row() < 0
            or index.row() >= len(self._visible_items)
        ):
            return None

        item = self._visible_items[index.row()]

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
        if (
            not index.isValid()
            or index.row() < 0
            or index.row() >= len(self._visible_items)
        ):
            return False

        if role == self.CheckStateRole:
            item = self._visible_items[index.row()]
            new_checked = value == Qt.CheckState.Checked
            if item.checked != new_checked:
                item.checked = new_checked
                self.dataChanged.emit(index, index, [self.CheckStateRole])
                self.apply_filter()
                self.active_mods_changed.emit()
            return True

        return False

    def set_check_states(self, rows: list[int], checked: bool) -> None:
        if not rows:
            return
        for row in rows:
            if 0 <= row < len(self._visible_items):
                self._visible_items[row].checked = checked
        top = min(rows)
        bottom = max(rows)
        self.dataChanged.emit(
            self.index(top, 0), self.index(bottom, 0), [self.CheckStateRole]
        )
        self.apply_filter()
        self.active_mods_changed.emit()

    def flags(self, index: QModelIndex | QPersistentModelIndex) -> Qt.ItemFlag:
        if not index.isValid():
            return Qt.ItemFlag.NoItemFlags | Qt.ItemFlag.ItemIsDropEnabled

        return (
            Qt.ItemFlag.ItemIsEnabled
            | Qt.ItemFlag.ItemIsSelectable
            | Qt.ItemFlag.ItemIsDragEnabled
            | Qt.ItemFlag.ItemIsUserCheckable
        )

    def roleNames(self) -> dict[int, QByteArray]:
        return {
            Qt.ItemDataRole.DisplayRole: QByteArray(b"name"),
            self.ModRole: QByteArray(b"mod"),
            self.CheckStateRole: QByteArray(b"checkState"),
            self.UuidRole: QByteArray(b"uuid"),
        }

    def supportedDropActions(self) -> Qt.DropAction:
        return Qt.DropAction.MoveAction

    def mimeTypes(self) -> list[str]:
        return [self.MIME_TYPE]

    def mimeData(self, indexes: Sequence[QModelIndex]) -> QMimeData:
        mime_data = QMimeData()
        rows = sorted({idx.row() for idx in indexes if idx.isValid()})
        mime_data.setData(self.MIME_TYPE, json.dumps(rows).encode("utf-8"))
        return mime_data

    def dropMimeData(
        self,
        data: QMimeData,
        action: Qt.DropAction,
        row: int,
        column: int,
        parent: QModelIndex | QPersistentModelIndex,
    ) -> bool:
        if action == Qt.DropAction.IgnoreAction:
            return True
        if not data.hasFormat(self.MIME_TYPE):
            return False

        source_rows: list[int] = json.loads(
            bytes(data.data(self.MIME_TYPE).data()).decode("utf-8")
        )
        target_row = (
            row
            if row != -1
            else (parent.row() if parent.isValid() else self.rowCount())
        )

        source_rows = [r for r in source_rows if 0 <= r < len(self._visible_items)]
        if not source_rows:
            return False

        first_src = source_rows[0]
        last_src = source_rows[-1]

        if not self.beginMoveRows(
            QModelIndex(), first_src, last_src, QModelIndex(), target_row
        ):
            return False

        moved_items = [self._visible_items[r] for r in source_rows]

        target_item = None
        if target_row < len(self._visible_items):
            target_item = self._visible_items[target_row]

        for item in moved_items:
            if item in self._all_items:
                self._all_items.remove(item)

        if target_item and target_item in self._all_items:
            master_target_idx = self._all_items.index(target_item)
        else:
            master_target_idx = len(self._all_items)

        for i, item in enumerate(moved_items):
            self._all_items.insert(master_target_idx + i, item)

        # Rebuild visible items without resetModel() since MoveRows already updated the view
        self._visible_items = []
        for item in self._all_items:
            if self._sidebar_uuids is not None and item.uuid not in self._sidebar_uuids:
                continue
            if self._search_text:
                name_match = self._search_text in item.mod.name.lower()
                pid_match = (
                    isinstance(item.mod, AboutXmlMod)
                    and self._search_text in str(item.mod.package_id).lower()
                )
                if not (name_match or pid_match):
                    continue
            self._visible_items.append(item)

        self.endMoveRows()

        QTimer.singleShot(0, self.active_mods_changed.emit)
        return True

    def load_mods(self, mods: dict[str, ListedMod], active_uuids: list[str]) -> None:
        # Block signals to prevent async GUI from reading broken model during build
        self.blockSignals(True)
        try:
            active_set = set(active_uuids)
            active_items = [
                ModItem(mod=mods[uuid], uuid=uuid, checked=True)
                for uuid in active_uuids
                if uuid in mods
            ]
            inactive_uuids = [
                uuid
                for uuid, _ in sorted(mods.items(), key=lambda kv: kv[1].name.lower())
                if uuid not in active_set
            ]
            inactive_items = [
                ModItem(mod=mods[uuid], uuid=uuid, checked=False)
                for uuid in inactive_uuids
            ]
            self._all_items = active_items + inactive_items
        finally:
            self.blockSignals(False)

        self.apply_filter()

    def active_uuids(self) -> list[str]:
        return [item.uuid for item in self._all_items if item.checked]

    def get_item_by_uuid(self, uuid: str) -> ModItem | None:
        for item in self._all_items:
            if item.uuid == uuid:
                return item
        return None

    def get_item(self, row: int) -> ModItem | None:
        if 0 <= row < len(self._visible_items):
            return self._visible_items[row]
        return None

    def reorder(self, ordered_uuids: list[str]) -> None:
        uuid_to_item = {item.uuid: item for item in self._all_items}
        new_items = []
        for uuid in ordered_uuids:
            if uuid in uuid_to_item:
                new_items.append(uuid_to_item.pop(uuid))
        new_items.extend(uuid_to_item.values())

        self._all_items = new_items
        self.apply_filter()
