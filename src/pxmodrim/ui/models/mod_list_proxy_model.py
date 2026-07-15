from __future__ import annotations

from typing import Any

from PySide6.QtCore import (
    QAbstractListModel,
    QByteArray,
    QModelIndex,
    QPersistentModelIndex,
    Qt,
)

from pxmodrim.core.models.metadata.structures import AboutXmlMod
from pxmodrim.ui.models.mod_list_model import ModListModel


class ModListProxyModel(QAbstractListModel):
    def __init__(self, source_model: ModListModel, parent: Any = None) -> None:
        super().__init__(parent)
        self._source = source_model
        self._search_text = ""
        self._sidebar_uuids: set[str] | None = None
        self._visible_uuids: list[str] = []
        self._source_row_for_uuid: dict[str, int] = {}
        self._in_proxy_move = False

        self._source.dataChanged.connect(self._on_source_data_changed)
        self._source.layoutChanged.connect(self._on_source_layout_changed)
        self._source.modelReset.connect(self._on_source_model_reset)
        self._source.rowsMoved.connect(self._on_source_rows_moved)
        self._source.rowsInserted.connect(self._on_source_rows_inserted)
        self._source.rowsRemoved.connect(self._on_source_rows_removed)

        self._rebuild()

    def _rebuild(self) -> None:
        self._rebuild_source_mapping()
        self._visible_uuids = [
            uuid
            for uuid, row in self._source_row_for_uuid.items()
            if self._accept_item(self._source.get_item(row))
        ]

    def _rebuild_source_mapping(self) -> None:
        self._source_row_for_uuid = {
            item.uuid: row
            for row in range(self._source.rowCount())
            if (item := self._source.get_item(row)) is not None
        }

    def _accept_item(self, item: Any) -> bool:
        if self._sidebar_uuids is not None and item.uuid not in self._sidebar_uuids:
            return False

        if self._search_text:
            name_match = self._search_text in item.mod.name.lower()
            pid_match = (
                isinstance(item.mod, AboutXmlMod)
                and self._search_text in str(item.mod.package_id).lower()
            )
            if not (name_match or pid_match):
                return False

        return True

    def _proxy_row_for_uuid(self, uuid: str) -> int | None:
        try:
            return self._visible_uuids.index(uuid)
        except ValueError:
            return None

    def set_search_filter(self, search_text: str) -> None:
        text = search_text.lower()
        if self._search_text == text:
            return
        self._search_text = text
        self._apply_filter_change()

    def set_sidebar_filter(self, uuids: set[str] | None) -> None:
        if self._sidebar_uuids == uuids:
            return
        self._sidebar_uuids = uuids
        self._apply_filter_change()

    def _apply_filter_change(self) -> None:
        self.layoutAboutToBeChanged.emit()
        self._rebuild()
        self.layoutChanged.emit()

    def rowCount(
        self, parent: QModelIndex | QPersistentModelIndex | None = None
    ) -> int:
        if parent is None:
            parent = QModelIndex()
        if parent.isValid():
            return 0
        return len(self._visible_uuids)

    def data(
        self,
        index: QModelIndex | QPersistentModelIndex,
        role: int = Qt.ItemDataRole.DisplayRole,
    ) -> Any:
        if (
            not index.isValid()
            or index.row() < 0
            or index.row() >= len(self._visible_uuids)
        ):
            return None
        source_row = self._source_row_for_uuid.get(self._visible_uuids[index.row()], -1)
        if source_row < 0:
            return None
        return self._source.data(self._source.index(source_row, 0), role)

    def setData(
        self,
        index: QModelIndex | QPersistentModelIndex,
        value: Any,
        role: int = Qt.ItemDataRole.EditRole,
    ) -> bool:
        if (
            not index.isValid()
            or index.row() < 0
            or index.row() >= len(self._visible_uuids)
        ):
            return False
        source_row = self._source_row_for_uuid.get(self._visible_uuids[index.row()], -1)
        if source_row < 0:
            return False
        return self._source.setData(self._source.index(source_row, 0), value, role)

    def flags(self, index: QModelIndex | QPersistentModelIndex) -> Qt.ItemFlag:
        if not index.isValid():
            return Qt.ItemFlag.NoItemFlags | Qt.ItemFlag.ItemIsDropEnabled
        source_row = self._source_row_for_uuid.get(self._visible_uuids[index.row()], -1)
        if source_row < 0:
            return Qt.ItemFlag.NoItemFlags
        return self._source.flags(self._source.index(source_row, 0))

    def roleNames(self) -> dict[int, QByteArray]:
        return self._source.roleNames()

    def mapToSource(self, proxy_index: QModelIndex) -> QModelIndex:
        if (
            not proxy_index.isValid()
            or proxy_index.row() < 0
            or proxy_index.row() >= len(self._visible_uuids)
        ):
            return QModelIndex()
        source_row = self._source_row_for_uuid.get(
            self._visible_uuids[proxy_index.row()], -1
        )
        if source_row < 0:
            return QModelIndex()
        return self._source.index(source_row, 0)

    def mapFromSource(self, source_index: QModelIndex) -> QModelIndex:
        if not source_index.isValid():
            return QModelIndex()
        item = self._source.get_item(source_index.row())
        if item is None:
            return QModelIndex()
        proxy_row = self._proxy_row_for_uuid(item.uuid)
        if proxy_row is None:
            return QModelIndex()
        return self.index(proxy_row, 0)

    def move_row(self, proxy_source: int, proxy_target: int) -> bool:
        if proxy_source == proxy_target:
            return False
        if not (0 <= proxy_source < len(self._visible_uuids)):
            return False
        if not (0 <= proxy_target < len(self._visible_uuids)):
            return False

        source_uuid = self._visible_uuids[proxy_source]
        target_uuid = self._visible_uuids[proxy_target]
        source_source_row = self._source_row_for_uuid.get(source_uuid, -1)
        source_target_row = self._source_row_for_uuid.get(target_uuid, -1)
        if source_source_row < 0 or source_target_row < 0:
            return False

        destination_child = (
            proxy_target + 1 if proxy_source < proxy_target else proxy_target
        )

        self._in_proxy_move = True
        try:
            self.beginMoveRows(
                QModelIndex(),
                proxy_source,
                proxy_source,
                QModelIndex(),
                destination_child,
            )
            self._visible_uuids.pop(proxy_source)
            self._visible_uuids.insert(proxy_target, source_uuid)
            self._source.move_row(source_source_row, source_target_row)
            self._rebuild_source_mapping()
            self.endMoveRows()
        finally:
            self._in_proxy_move = False
        return True

    def _on_source_data_changed(
        self, top_left: QModelIndex, bottom_right: QModelIndex, roles: list[int]
    ) -> None:
        first = top_left.row()
        last = bottom_right.row()
        proxy_rows: list[int] = []
        for source_row in range(first, last + 1):
            item = self._source.get_item(source_row)
            if item is None:
                continue
            proxy_row = self._proxy_row_for_uuid(item.uuid)
            if proxy_row is not None:
                proxy_rows.append(proxy_row)
        if not proxy_rows:
            return
        top = self.index(min(proxy_rows), 0)
        bottom = self.index(max(proxy_rows), 0)
        self.dataChanged.emit(top, bottom, roles)

    def _on_source_layout_changed(self) -> None:
        if self._in_proxy_move:
            return
        self.layoutAboutToBeChanged.emit()
        self._rebuild()
        self.layoutChanged.emit()

    def _on_source_model_reset(self) -> None:
        self.beginResetModel()
        self._rebuild()
        self.endResetModel()

    def _on_source_rows_moved(
        self,
        parent: QModelIndex,
        start: int,
        end: int,
        destination: QModelIndex,
        row: int,
    ) -> None:
        if self._in_proxy_move:
            return
        self._on_source_layout_changed()

    def _on_source_rows_inserted(
        self, parent: QModelIndex, first: int, last: int
    ) -> None:
        if self._in_proxy_move:
            return
        self._on_source_layout_changed()

    def _on_source_rows_removed(
        self, parent: QModelIndex, first: int, last: int
    ) -> None:
        if self._in_proxy_move:
            return
        self._on_source_layout_changed()
