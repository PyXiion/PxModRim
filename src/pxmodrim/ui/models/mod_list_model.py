from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Any

from PySide6.QtCore import (
    QAbstractListModel,
    QByteArray,
    QModelIndex,
    QPersistentModelIndex,
    Qt,
    Signal,
)

from pxmodrim.core.models.metadata.structures import AboutXmlMod, ListedMod

if TYPE_CHECKING:
    pass


@dataclass
class ModItem:
    mod: ListedMod
    uuid: str
    checked: bool = False
    provider_color: str = "#808080"
    has_error: bool = False
    has_warning: bool = False
    error_tooltip: str = ""
    warning_tooltip: str = ""


class ModListModel(QAbstractListModel):
    CheckStateRole = Qt.ItemDataRole.UserRole + 1
    PackageIdRole = Qt.ItemDataRole.UserRole + 2
    ModVersionRole = Qt.ItemDataRole.UserRole + 3
    ProviderColorRole = Qt.ItemDataRole.UserRole + 4
    UuidRole = Qt.ItemDataRole.UserRole + 5
    HasErrorRole = Qt.ItemDataRole.UserRole + 6
    HasWarningRole = Qt.ItemDataRole.UserRole + 7
    ErrorTooltipRole = Qt.ItemDataRole.UserRole + 8
    WarningTooltipRole = Qt.ItemDataRole.UserRole + 9

    active_mods_changed = Signal()

    def __init__(self, provider_colors: dict[str, str]) -> None:
        super().__init__()
        self._provider_colors = provider_colors
        self._items: list[ModItem] = []

    def rowCount(
        self, parent: QModelIndex | QPersistentModelIndex | None = None
    ) -> int:
        if parent is None:
            parent = QModelIndex()
        if parent.isValid():
            return 0
        return len(self._items)

    def data(
        self,
        index: QModelIndex | QPersistentModelIndex,
        role: int = Qt.ItemDataRole.DisplayRole,
    ) -> Any:
        if (
            not index.isValid()
            or index.row() < 0
            or index.row() >= len(self._items)
        ):
            return None

        item = self._items[index.row()]

        if role == Qt.ItemDataRole.DisplayRole:
            return item.mod.name
        if role == self.CheckStateRole:
            return Qt.CheckState.Checked if item.checked else Qt.CheckState.Unchecked
        if role == self.PackageIdRole:
            return str(item.mod.package_id) if isinstance(item.mod, AboutXmlMod) else ""
        if role == self.ModVersionRole:
            return item.mod.mod_version if isinstance(item.mod, AboutXmlMod) else ""
        if role == self.ProviderColorRole:
            return item.provider_color
        if role == self.UuidRole:
            return item.uuid
        if role == self.HasErrorRole:
            return 1 if item.has_error else 0
        if role == self.HasWarningRole:
            return 1 if item.has_warning else 0
        if role == self.ErrorTooltipRole:
            return item.error_tooltip
        if role == self.WarningTooltipRole:
            return item.warning_tooltip
        if (
            role == Qt.ItemDataRole.ToolTipRole
            and hasattr(item.mod, "description")
            and item.mod.description
        ):
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
            or index.row() >= len(self._items)
        ):
            return False

        if role == self.CheckStateRole:
            item = self._items[index.row()]
            new_checked = value == Qt.CheckState.Checked
            if item.checked == new_checked:
                return True
            item.checked = new_checked
            self.dataChanged.emit(index, index, [self.CheckStateRole])
            self.active_mods_changed.emit()
            return True

        return False

    def set_diagnostics(self, diagnostics: dict[str, Any]) -> None:
        changed_rows: list[int] = []
        for row, item in enumerate(self._items):
            old_flags = (item.has_error, item.has_warning)
            got = diagnostics.get(item.uuid)
            if got is not None:
                item.has_error = got.has_errors
                item.has_warning = got.has_warnings
                item.error_tooltip = got.error_tooltip
                item.warning_tooltip = got.warning_tooltip
            else:
                item.has_error = False
                item.has_warning = False
                item.error_tooltip = ""
                item.warning_tooltip = ""
            if (item.has_error, item.has_warning) != old_flags:
                changed_rows.append(row)

        if not changed_rows:
            return

        top = self.index(min(changed_rows), 0)
        bottom = self.index(max(changed_rows), 0)
        self.dataChanged.emit(
            top,
            bottom,
            [
                self.HasErrorRole,
                self.HasWarningRole,
                self.ErrorTooltipRole,
                self.WarningTooltipRole,
            ],
        )

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
            self.CheckStateRole: QByteArray(b"checkState"),
            self.PackageIdRole: QByteArray(b"packageId"),
            self.ModVersionRole: QByteArray(b"modVersion"),
            self.ProviderColorRole: QByteArray(b"providerColor"),
            self.UuidRole: QByteArray(b"uuid"),
            self.HasErrorRole: QByteArray(b"hasError"),
            self.HasWarningRole: QByteArray(b"hasWarning"),
            self.ErrorTooltipRole: QByteArray(b"errorTooltip"),
            self.WarningTooltipRole: QByteArray(b"warningTooltip"),
            Qt.ItemDataRole.ToolTipRole: QByteArray(b"toolTip"),
        }

    def update_provider_colors(self, provider_colors: dict[str, str]) -> None:
        self._provider_colors = provider_colors

    def move_row(self, source_row: int, target_row: int) -> bool:
        if source_row == target_row:
            return False
        if not (0 <= source_row < len(self._items)):
            return False
        if not (0 <= target_row < len(self._items)):
            return False

        parent = QModelIndex()
        destination_child = target_row + 1 if source_row < target_row else target_row
        self.beginMoveRows(parent, source_row, source_row, parent, destination_child)
        item = self._items.pop(source_row)
        self._items.insert(target_row, item)
        self.endMoveRows()
        return True

    def commitOrder(self, new_ordered_uuids: list[str]) -> None:
        if not new_ordered_uuids:
            return

        item_by_uuid = {item.uuid: item for item in self._items}
        existing_uuids = [uuid for uuid in new_ordered_uuids if uuid in item_by_uuid]
        if not existing_uuids:
            return

        ordered_set = set(existing_uuids)
        ordered_items = [item_by_uuid[uuid] for uuid in existing_uuids]
        remaining = [item for item in self._items if item.uuid not in ordered_set]
        new_items = ordered_items + remaining

        if new_items == self._items:
            return

        old_uuid_to_row = {item.uuid: i for i, item in enumerate(self._items)}
        new_row_for_old = {
            old_uuid_to_row[item.uuid]: i for i, item in enumerate(new_items)
        }

        persistent_indexes = self.persistentIndexList()
        new_persistent: list[QModelIndex] = []
        for pid in persistent_indexes:
            new_row = new_row_for_old.get(pid.row(), -1)
            new_persistent.append(self.index(new_row, 0))
        if persistent_indexes:
            self.changePersistentIndexList(persistent_indexes, new_persistent)

        self._items = new_items
        self.layoutChanged.emit()

    def reorder(self, ordered_uuids: list[str]) -> None:
        self.commitOrder(ordered_uuids)

    def load_mods(self, mods: dict[str, ListedMod], active_uuids: list[str]) -> None:
        self.beginResetModel()
        try:
            active_set = set(active_uuids)
            active_items = [
                ModItem(
                    mod=mods[uuid],
                    uuid=uuid,
                    checked=True,
                    provider_color=self._provider_colors.get(
                        mods[uuid].provider_id, "#808080"
                    ),
                )
                for uuid in active_uuids
                if uuid in mods
            ]
            inactive_uuids = [
                uuid
                for uuid, _ in sorted(mods.items(), key=lambda kv: kv[1].name.lower())
                if uuid not in active_set
            ]
            inactive_items = [
                ModItem(
                    mod=mods[uuid],
                    uuid=uuid,
                    checked=False,
                    provider_color=self._provider_colors.get(
                        mods[uuid].provider_id, "#808080"
                    ),
                )
                for uuid in inactive_uuids
            ]
            self._items = active_items + inactive_items
        finally:
            self.endResetModel()

    def active_uuids(self) -> list[str]:
        return [item.uuid for item in self._items if item.checked]

    def get_item(self, row: int) -> ModItem | None:
        if 0 <= row < len(self._items):
            return self._items[row]
        return None

    def get_item_by_uuid(self, uuid: str) -> ModItem | None:
        for item in self._items:
            if item.uuid == uuid:
                return item
        return None

    def set_check_states(self, rows: list[int], checked: bool) -> None:
        if not rows:
            return
        changed_rows: list[int] = []
        for row in rows:
            if (
                0 <= row < len(self._items)
                and self._items[row].checked != checked
            ):
                self._items[row].checked = checked
                changed_rows.append(row)
        if not changed_rows:
            return
        top = self.index(min(changed_rows), 0)
        bottom = self.index(max(changed_rows), 0)
        self.dataChanged.emit(top, bottom, [self.CheckStateRole])
        self.active_mods_changed.emit()
