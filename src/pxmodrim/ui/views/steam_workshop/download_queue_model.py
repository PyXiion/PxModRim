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

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._items: list[dict[str, str]] = []

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
        return None

    def roleNames(self) -> dict[int, QByteArray]:
        return {
            self._IdRole: QByteArray(b"id"),
            self._TitleRole: QByteArray(b"title"),
            self._DisplayRole: QByteArray(b"display"),
        }

    def sync_from(self, checked_ids: dict[str, str]) -> None:
        self.beginResetModel()
        self._items = [
            {"id": mid, "title": t or mid, "display": f"{t} ({mid})" if t else mid}
            for mid, t in checked_ids.items()
        ]
        self.endResetModel()

    @property
    def count(self) -> int:
        return len(self._items)
