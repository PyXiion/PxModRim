from __future__ import annotations

from typing import Any

from PySide6.QtCore import (
    Property,
    QAbstractListModel,
    QByteArray,
    QModelIndex,
    QObject,
    QPersistentModelIndex,
    Qt,
    Signal,
)


class DownloadQueueModel(QAbstractListModel):
    _IdRole = Qt.ItemDataRole.UserRole + 1
    _TitleRole = Qt.ItemDataRole.UserRole + 2
    _DisplayRole = Qt.ItemDataRole.UserRole + 3
    _StatusRole = Qt.ItemDataRole.UserRole + 4
    _ProgressRole = Qt.ItemDataRole.UserRole + 5

    progress_changed = Signal()

    def __init__(self, parent: QObject | None = None) -> None:
        """Initialize the download queue model with empty state."""
        super().__init__(parent)
        self._items: list[dict[str, Any]] = []
        self._progress_total = 0
        self._progress_completed = 0
        self._bytes_done = 0
        self._bytes_total = 0

    @Property(int, notify=progress_changed)
    def progress_total(self) -> int:
        return self._progress_total

    @Property(int, notify=progress_changed)
    def progress_completed(self) -> int:
        return self._progress_completed

    @Property(float, notify=progress_changed)
    def bytes_done(self) -> float:
        return float(self._bytes_done)

    @Property(float, notify=progress_changed)
    def bytes_total(self) -> float:
        return float(self._bytes_total)

    def set_progress(
        self, total: int, completed: int, bytes_done: int = 0, bytes_total: int = 0
    ) -> None:
        self._progress_total = total
        self._progress_completed = completed
        self._bytes_done = bytes_done
        self._bytes_total = bytes_total
        self.progress_changed.emit()

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
        self.set_progress(0, 0)

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
