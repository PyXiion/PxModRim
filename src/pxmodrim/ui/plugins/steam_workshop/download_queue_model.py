from __future__ import annotations

from typing import Any

from PySide6.QtCore import (
    QAbstractListModel,
    QByteArray,
    QModelIndex,
    QObject,
    QPersistentModelIndex,
    Qt,
)


class DownloadQueueModel(QAbstractListModel):
    _IdRole = Qt.ItemDataRole.UserRole + 1
    _TitleRole = Qt.ItemDataRole.UserRole + 2
    _DisplayRole = Qt.ItemDataRole.UserRole + 3
    _StatusRole = Qt.ItemDataRole.UserRole + 4
    _ProgressRole = Qt.ItemDataRole.UserRole + 5

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._items: list[dict[str, Any]] = []

    def rowCount(
        self,
        parent: QModelIndex | QPersistentModelIndex | None = None,
    ) -> int:
        return len(self._items) if parent is None or not parent.isValid() else 0

    def data(
        self,
        index: QModelIndex | QPersistentModelIndex,
        role: int = Qt.ItemDataRole.DisplayRole,
    ) -> Any:
        if not index.isValid() or index.row() >= len(self._items):
            return None
        item = self._items[index.row()]
        if role == self._IdRole:
            return item["id"]
        if role == self._TitleRole:
            return item["title"]
        if role == self._DisplayRole:
            return item["display"]
        if role == self._StatusRole:
            return item["status"]
        if role == self._ProgressRole:
            return item["progress"]
        return None

    def roleNames(self) -> dict[int, QByteArray]:
        return {
            self._IdRole: QByteArray(b"id"),
            self._TitleRole: QByteArray(b"title"),
            self._DisplayRole: QByteArray(b"display"),
            self._StatusRole: QByteArray(b"status"),
            self._ProgressRole: QByteArray(b"progress"),
        }

    def sync_from(
        self,
        checked_ids: dict[str, str],
        statuses: dict[str, str] | None = None,
    ) -> None:
        statuses = statuses or {}
        self.beginResetModel()
        self._items = [
            {
                "id": mid,
                "title": t or mid,
                "display": f"{t} ({mid})" if t else mid,
                "status": statuses.get(mid, ""),
                "progress": 0.0,
            }
            for mid, t in checked_ids.items()
        ]
        self.endResetModel()

    def update_status(
        self, mod_id: str, status: str, bytes_done: int = 0, bytes_total: int = 0
    ) -> None:
        for row, item in enumerate(self._items):
            if item["id"] == mod_id:
                item["status"] = status
                item["progress"] = bytes_done / bytes_total if bytes_total else 0.0
                idx = self.index(row)
                self.dataChanged.emit(idx, idx, [self._StatusRole, self._ProgressRole])
                return

    @property
    def count(self) -> int:
        return len(self._items)
