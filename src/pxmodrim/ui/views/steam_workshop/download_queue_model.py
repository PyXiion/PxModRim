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

    progress_total_changed = Signal(int)
    progress_completed_changed = Signal(int)
    downloading_id_changed = Signal(str)

    def __init__(self, parent: QObject | None = None) -> None:
        """Initialize the download queue model with empty state."""
        super().__init__(parent)
        self._items: list[dict[str, str]] = []
        self._progress_total = 0
        self._progress_completed = 0
        self._downloading_id = ""

    @Property(int, notify=progress_total_changed)
    def progress_total(self) -> int:
        return self._progress_total

    @Property(int, notify=progress_completed_changed)
    def progress_completed(self) -> int:
        return self._progress_completed

    @Property(str, notify=downloading_id_changed)
    def downloading_id(self) -> str:
        return self._downloading_id

    def set_progress(self, total: int, completed: int, downloading_id: str) -> None:
        self._progress_total = total
        self._progress_completed = completed
        self._downloading_id = downloading_id
        self.progress_total_changed.emit(total)
        self.progress_completed_changed.emit(completed)
        self.downloading_id_changed.emit(downloading_id)

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
        return None

    def roleNames(self) -> dict[int, QByteArray]:
        return {
            self._IdRole: QByteArray(b"id"),
            self._TitleRole: QByteArray(b"title"),
            self._DisplayRole: QByteArray(b"display"),
            self._StatusRole: QByteArray(b"status"),
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
            }
            for mid, t in checked_ids.items()
        ]
        self.endResetModel()
        self.set_progress(0, 0, "")

    def update_status(self, mod_id: str, status: str) -> None:
        for row, item in enumerate(self._items):
            if item["id"] == mod_id:
                item["status"] = status
                idx = self.index(row)
                self.dataChanged.emit(idx, idx, [self._StatusRole])
                return

    @property
    def count(self) -> int:
        return len(self._items)
