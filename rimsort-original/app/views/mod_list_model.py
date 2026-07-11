from __future__ import annotations

import json
from typing import Any

from PySide6.QtCore import QAbstractListModel, QMimeData, QModelIndex, QObject, Qt

from app.models.divider import (
    DividerData,
    generate_divider_uuid,
    is_divider_uuid,
)

UUID_ROLE = Qt.ItemDataRole.UserRole + 1
IS_DIVIDER_ROLE = Qt.ItemDataRole.UserRole + 2
COLLAPSED_ROLE = Qt.ItemDataRole.UserRole + 3


class ModListModel(QAbstractListModel):
    """
    A QAbstractListModel that stores a flat list of mod UUIDs.
    Dividers are identified by UUIDs starting with DIVIDER_UUID_PREFIX.
    """

    MIME_TYPE = "application/x-rimsort-mod-index"

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self.items: list[str] = []
        self._divider_data: dict[str, DividerData] = {}

    # --- Data manipulation ---

    def set_items(self, uuids: list[str]) -> None:
        self.beginResetModel()
        self.items = list(uuids)
        self._divider_data = {
            u: self._divider_data.get(u, DividerData(u, ""))
            for u in uuids
            if is_divider_uuid(u)
        }
        self.endResetModel()

    def get_all_uuids(self) -> list[str]:
        return list(self.items)

    def get_all_mod_uuids(self) -> list[str]:
        return [u for u in self.items if not is_divider_uuid(u)]

    def insert_uuids(self, row: int, uuids: list[str]) -> None:
        if not uuids:
            return
        self.beginInsertRows(QModelIndex(), row, row + len(uuids) - 1)
        for i, u in enumerate(uuids):
            self.items.insert(row + i, u)
        self.endInsertRows()

    def remove_uuids(self, row: int, count: int) -> None:
        if count <= 0:
            return
        self.beginRemoveRows(QModelIndex(), row, row + count - 1)
        del self.items[row : row + count]
        self.endRemoveRows()

    def remove_uuids_by_value(self, uuids: set[str]) -> None:
        rows_to_remove = [i for i, u in enumerate(self.items) if u in uuids]
        for i in reversed(rows_to_remove):
            self.beginRemoveRows(QModelIndex(), i, i)
            del self.items[i]
            self.endRemoveRows()

    # --- Dividers ---

    def divider_count(self) -> int:
        return sum(1 for u in self.items if is_divider_uuid(u))

    def insert_divider(self, row: int, name: str = "") -> str:
        uuid = generate_divider_uuid()
        data = DividerData(uuid=uuid, name=name)
        self._divider_data[uuid] = data
        self.beginInsertRows(QModelIndex(), row, row)
        self.items.insert(row, uuid)
        self.endInsertRows()
        return uuid

    def set_divider_name(self, uuid: str, name: str) -> None:
        if uuid in self._divider_data:
            self._divider_data[uuid].name = name
            try:
                row = self.items.index(uuid)
                idx = self.index(row)
                self.dataChanged.emit(idx, idx, [Qt.ItemDataRole.DisplayRole])
            except ValueError:
                pass

    def set_divider_collapsed(self, uuid: str, collapsed: bool) -> None:
        if uuid in self._divider_data:
            self._divider_data[uuid].collapsed = collapsed
            try:
                row = self.items.index(uuid)
                idx = self.index(row)
                self.dataChanged.emit(idx, idx, [COLLAPSED_ROLE])
            except ValueError:
                pass

    def get_divider_data(self) -> list[dict[str, Any]]:
        result = []
        for i, u in enumerate(self.items):
            if is_divider_uuid(u):
                dd = self._divider_data.get(u)
                if dd:
                    result.append(
                        {
                            "uuid": dd.uuid,
                            "name": dd.name,
                            "collapsed": dd.collapsed,
                            "index": i,
                        }
                    )
        return result

    # --- QAbstractListModel interface ---

    def rowCount(self, parent: QModelIndex = QModelIndex()) -> int:
        return len(self.items)

    def data(self, index: QModelIndex, role: int = Qt.ItemDataRole.DisplayRole) -> Any:
        if not index.isValid() or not (0 <= index.row() < len(self.items)):
            return None

        uuid = self.items[index.row()]

        if role == UUID_ROLE:
            return uuid
        if role == IS_DIVIDER_ROLE:
            return is_divider_uuid(uuid)
        if role == COLLAPSED_ROLE:
            if is_divider_uuid(uuid):
                dd = self._divider_data.get(uuid)
                return dd.collapsed if dd else False
            return None
        if role == Qt.ItemDataRole.DisplayRole and is_divider_uuid(uuid):
            dd = self._divider_data.get(uuid)
            return dd.name if dd else ""
        if role == Qt.ItemDataRole.ToolTipRole:
            if is_divider_uuid(uuid):
                return ""
            return uuid  # let consumer resolve to mod name

        return None

    def setData(
        self,
        index: QModelIndex,
        value: Any,
        role: int = Qt.ItemDataRole.EditRole,
    ) -> bool:
        if not index.isValid() or not (0 <= index.row() < len(self.items)):
            return False
        uuid = self.items[index.row()]
        if role == Qt.ItemDataRole.EditRole and is_divider_uuid(uuid):
            if uuid in self._divider_data:
                self._divider_data[uuid].name = str(value)
                self.dataChanged.emit(index, index, [Qt.ItemDataRole.DisplayRole])
                return True
        return False

    def flags(self, index: QModelIndex) -> Qt.ItemFlag:
        default_flags = super().flags(index)
        if not index.isValid():
            return default_flags | Qt.ItemFlag.ItemIsDropEnabled
        if is_divider_uuid(self.items[index.row()]):
            return default_flags
        return (
            default_flags
            | Qt.ItemFlag.ItemIsDragEnabled
            | Qt.ItemFlag.ItemIsDropEnabled
        )

    def supportedDropActions(self) -> Qt.DropAction:
        return Qt.DropAction.MoveAction

    def mimeTypes(self) -> list[str]:
        return [self.MIME_TYPE]

    def mimeData(self, indexes: list[QModelIndex]) -> QMimeData:
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
        parent: QModelIndex,
    ) -> bool:
        if action == Qt.DropAction.IgnoreAction:
            return True
        if not data.hasFormat(self.MIME_TYPE):
            return False

        source_rows: list[int] = json.loads(bytes(data.data(self.MIME_TYPE)))

        target_row = (
            row
            if row != -1
            else (parent.row() if parent.isValid() else self.rowCount())
        )

        moved_uuids = [self.items[r] for r in source_rows]

        for r in reversed(source_rows):
            self.beginRemoveRows(QModelIndex(), r, r)
            self.items.pop(r)
            self.endRemoveRows()
            if r < target_row:
                target_row -= 1

        self.beginInsertRows(
            QModelIndex(), target_row, target_row + len(moved_uuids) - 1
        )
        for i, u in enumerate(moved_uuids):
            self.items.insert(target_row + i, u)
        self.endInsertRows()

        return True

    def removeRows(
        self,
        row: int,
        count: int,
        parent: QModelIndex = QModelIndex(),
    ) -> bool:
        if row < 0 or row + count > len(self.items):
            return False
        self.beginRemoveRows(parent, row, row + count - 1)
        del self.items[row : row + count]
        self.endRemoveRows()
        return True
