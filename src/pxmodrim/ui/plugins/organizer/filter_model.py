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

from pxmodrim.core.organizer import TreeFilter

_ICONS = {"all": "grid", "active": "check-circle", "inactive": "x-circle"}
_PROVIDER_ICONS = {
    "steam": "steam",
    "downloaded": "steam",
    "core": "grid",
    "git": "git",
}
_TAG_ICON = "tag"


class OrganizerFilterModel(QAbstractListModel):
    LabelRole = Qt.ItemDataRole.UserRole + 1
    CountRole = Qt.ItemDataRole.UserRole + 2
    IconRole = Qt.ItemDataRole.UserRole + 3
    SectionRole = Qt.ItemDataRole.UserRole + 4
    KeyRole = Qt.ItemDataRole.UserRole + 5

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._items: list[TreeFilter] = []

    def rowCount(
        self, parent: QModelIndex | QPersistentModelIndex | None = None
    ) -> int:
        return 0 if parent is not None and parent.isValid() else len(self._items)

    def data(
        self,
        index: QModelIndex | QPersistentModelIndex,
        role: int = Qt.ItemDataRole.DisplayRole,
    ) -> Any:
        if not index.isValid() or not 0 <= index.row() < len(self._items):
            return None
        entry = self._items[index.row()]
        if role in (self.LabelRole, Qt.ItemDataRole.DisplayRole):
            return entry.label
        if role == self.CountRole:
            return entry.count
        if role == self.IconRole:
            if entry.tag_id is not None or entry.key.startswith("tag:"):
                return _TAG_ICON
            return _ICONS.get(
                entry.key, _PROVIDER_ICONS.get(entry.provider_id or "", "folder")
            )
        if role == self.SectionRole:
            if entry.tag_id is not None or entry.key.startswith("tag:"):
                return "Tags"
            if entry.provider_id is not None or entry.key.startswith("provider:"):
                return "Providers"
            return "Status"
        if role == self.KeyRole:
            return entry.key
        return None

    def roleNames(self) -> dict[int, QByteArray]:
        return {
            Qt.ItemDataRole.DisplayRole: QByteArray(b"filterLabel"),
            self.CountRole: QByteArray(b"count"),
            self.IconRole: QByteArray(b"iconName"),
            self.SectionRole: QByteArray(b"sectionName"),
            self.KeyRole: QByteArray(b"key"),
        }

    def update(self, entries: list[TreeFilter]) -> None:
        if [item.key for item in self._items] != [item.key for item in entries]:
            self.beginResetModel()
            self._items = entries
            self.endResetModel()
        else:
            self._items = entries
            if entries:
                self.dataChanged.emit(
                    self.index(0),
                    self.index(len(entries) - 1),
                    [self.CountRole, self.LabelRole, Qt.ItemDataRole.DisplayRole],
                )

    def for_key(self, key: str) -> TreeFilter:
        for item in self._items:
            if item.key == key:
                return item
        if self._items:
            return self._items[0]
        return TreeFilter("all", "All", 0)
