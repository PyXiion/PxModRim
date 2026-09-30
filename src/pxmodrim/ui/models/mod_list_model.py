from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Any

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

from pxmodrim.core.models.metadata.structures import AboutXmlMod, ListedMod
from pxmodrim.ui.models.impact import format_duration, impact_color
from pxmodrim.ui.theme.palette import PALETTE

if TYPE_CHECKING:
    from pxmodrim.core.services.diagnostics_service import DiagnosticsService


def provider_label(provider_id: str, package_id: str = "") -> str:
    pid = provider_id.lower().strip()
    pkg = package_id.lower().strip()
    if pid in {"steam", "downloaded"}:
        return "Workshop"
    if pid == "local":
        return "Local"
    if pid == "core":
        if pkg and pkg != "ludeon.rimworld":
            return "DLC"
        return "Core"
    if pkg.startswith("ludeon.rimworld.") and pkg != "ludeon.rimworld":
        return "DLC"
    if pkg == "ludeon.rimworld":
        return "Core"
    return pid.capitalize() if pid else ""


def section_name(checked: bool, active_count: int, inactive_count: int) -> str:
    if checked or active_count == 0 or inactive_count == 0:
        return ""
    return f"Inactive ({inactive_count})"


@dataclass(slots=True)
class ModItem:
    mod: ListedMod
    uuid: str
    checked: bool = False
    provider_color: str = PALETTE["TEXT_DIM"]
    startup_impact_s: float = 0.0
    load_index: int = -1
    provider_label: str = ""


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
    StartupImpactRole = Qt.ItemDataRole.UserRole + 10
    LoadIndexRole = Qt.ItemDataRole.UserRole + 11
    IsActiveRole = Qt.ItemDataRole.UserRole + 12
    SectionNameRole = Qt.ItemDataRole.UserRole + 13
    ProviderLabelRole = Qt.ItemDataRole.UserRole + 14
    StartupImpactColorRole = Qt.ItemDataRole.UserRole + 15
    StartupImpactTextRole = Qt.ItemDataRole.UserRole + 16
    active_mods_changed = Signal()
    active_count_changed = Signal()

    @Property(int, notify=active_count_changed)
    def activeCount(self) -> int:
        return self._active_count

    def __init__(
        self,
        provider_colors: dict[str, str],
        diagnostics: DiagnosticsService | None = None,
        parent: QObject | None = None,
    ) -> None:
        super().__init__(parent)
        self._provider_colors = provider_colors
        self._items: list[ModItem] = []
        self._active_count: int = 0
        self._diag = diagnostics
        if diagnostics is not None:
            diagnostics.diagnostics_summary_changed.connect(
                self._on_diagnostics_summary_changed
            )

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
        if not index.isValid() or index.row() < 0 or index.row() >= len(self._items):
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
        if role in {
            self.HasErrorRole,
            self.HasWarningRole,
            self.ErrorTooltipRole,
            self.WarningTooltipRole,
        }:
            diag = self._diag.summary_for(item.uuid) if self._diag else None
            if role == self.HasErrorRole:
                return 1 if diag is not None and diag.has_errors else 0
            if role == self.HasWarningRole:
                return 1 if diag is not None and diag.has_warnings else 0
            if role == self.ErrorTooltipRole:
                return diag.error_tooltip if diag is not None else ""
            if role == self.WarningTooltipRole:
                return diag.warning_tooltip if diag is not None else ""
        if role == self.StartupImpactRole:
            return item.startup_impact_s
        if role == self.StartupImpactColorRole:
            return impact_color(item.startup_impact_s)
        if role == self.StartupImpactTextRole:
            return format_duration(item.startup_impact_s)
        if role == self.LoadIndexRole:
            return item.load_index
        if role == self.IsActiveRole:
            return item.checked
        if role == self.SectionNameRole:
            return section_name(
                item.checked, self._active_count, len(self._items) - self._active_count
            )
        if role == self.ProviderLabelRole:
            if item.provider_label:
                return item.provider_label
            pkg = str(getattr(item.mod, "package_id", ""))
            return provider_label(item.mod.provider_id, pkg)
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
        if not index.isValid() or index.row() < 0 or index.row() >= len(self._items):
            return False

        if role == self.CheckStateRole:
            self.set_check_states([index.row()], value == Qt.CheckState.Checked)
            return True

        return False

    def set_startup_impact(self, impacts: dict[str, float] | None = None) -> None:
        if not self._items:
            return
        if impacts is None:
            impacts = {}
        for item in self._items:
            mod = item.mod
            pid = getattr(mod, "package_id", None)
            item.startup_impact_s = (
                impacts.get(str(pid), 0.0) if pid is not None else 0.0
            )

        top = self.index(0, 0)
        bottom = self.index(len(self._items) - 1, 0)
        self.dataChanged.emit(
            top,
            bottom,
            [
                self.StartupImpactRole,
                self.StartupImpactColorRole,
                self.StartupImpactTextRole,
            ],
        )

    def _on_diagnostics_summary_changed(self, diags: dict[str, Any]) -> None:
        if not self._items:
            return
        old = getattr(self, "_last_diags", {}) or {}
        self._last_diags = diags
        changed = [
            row
            for row, item in enumerate(self._items)
            if old.get(item.uuid) != diags.get(item.uuid)
        ]
        if not changed:
            return
        top = self.index(min(changed), 0)
        bottom = self.index(max(changed), 0)
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
            self.StartupImpactRole: QByteArray(b"startupImpact"),
            self.LoadIndexRole: QByteArray(b"loadIndex"),
            self.IsActiveRole: QByteArray(b"isActive"),
            self.SectionNameRole: QByteArray(b"sectionName"),
            self.ProviderLabelRole: QByteArray(b"providerLabel"),
            self.StartupImpactColorRole: QByteArray(b"startupImpactColor"),
            self.StartupImpactTextRole: QByteArray(b"startupImpactText"),
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
        changed = self._update_load_indices()
        if changed:
            top = self.index(min(changed), 0)
            bottom = self.index(max(changed), 0)
            self.dataChanged.emit(
                top,
                bottom,
                [self.LoadIndexRole, self.SectionNameRole],
            )
        return True

    def commitOrder(self, new_ordered_uuids: list[str]) -> None:
        item_by_uuid = {item.uuid: item for item in self._items}
        existing_uuids = [uuid for uuid in new_ordered_uuids if uuid in item_by_uuid]

        ordered_set = set(existing_uuids)
        ordered_items = [item_by_uuid[uuid] for uuid in existing_uuids]
        remaining = [item for item in self._items if item.uuid not in ordered_set]
        # Keep the load_mods invariant: active block first, inactive block by name.
        new_items = (
            ordered_items
            + [item for item in remaining if item.checked]
            + sorted(
                (item for item in remaining if not item.checked),
                key=lambda item: item.mod.name.lower(),
            )
        )

        if new_items == self._items:
            return

        old_uuid_to_row = {item.uuid: i for i, item in enumerate(self._items)}
        new_row_for_old = {
            old_uuid_to_row[item.uuid]: i for i, item in enumerate(new_items)
        }

        self.layoutAboutToBeChanged.emit()
        persistent_indexes = self.persistentIndexList()
        self._items = new_items
        self._update_load_indices()
        new_persistent = [
            self.index(new_row_for_old.get(index.row(), -1), 0)
            for index in persistent_indexes
        ]
        if persistent_indexes:
            self.changePersistentIndexList(persistent_indexes, new_persistent)
        self.layoutChanged.emit()

    def _make_item(self, uuid: str, mod: ListedMod, checked: bool) -> ModItem:
        return ModItem(
            mod=mod,
            uuid=uuid,
            checked=checked,
            provider_color=self._provider_colors.get(
                mod.provider_id, PALETTE["TEXT_DIM"]
            ),
            provider_label=provider_label(
                mod.provider_id, str(getattr(mod, "package_id", ""))
            ),
        )

    def load_mods(self, mods: dict[str, ListedMod], active_uuids: list[str]) -> None:
        self.beginResetModel()
        try:
            active_set = set(active_uuids)
            inactive = sorted(
                ((uuid, mod) for uuid, mod in mods.items() if uuid not in active_set),
                key=lambda kv: kv[1].name.lower(),
            )
            self._items = [
                self._make_item(uuid, mods[uuid], True)
                for uuid in active_uuids
                if uuid in mods
            ] + [self._make_item(uuid, mod, False) for uuid, mod in inactive]
            self._update_load_indices()
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

    def set_checkable(self, uuids: frozenset[str], checked: bool) -> None:
        if not uuids:
            return
        rows = [
            i
            for i, it in enumerate(self._items)
            if it.uuid in uuids and it.checked != checked
        ]
        self.set_check_states(rows, checked)

    def set_check_states(self, rows: list[int], checked: bool) -> None:
        if not rows:
            return
        changed_rows: list[int] = []
        for row in rows:
            if 0 <= row < len(self._items) and self._items[row].checked != checked:
                self._items[row].checked = checked
                changed_rows.append(row)
        if not changed_rows:
            return
        load_changed = self._update_load_indices()
        all_affected = set(changed_rows) | set(load_changed)
        top = self.index(min(all_affected), 0)
        bottom = self.index(len(self._items) - 1, 0)
        self.dataChanged.emit(
            top,
            bottom,
            [
                self.CheckStateRole,
                self.LoadIndexRole,
                self.IsActiveRole,
                self.SectionNameRole,
            ],
        )
        self.active_mods_changed.emit()

    def _update_load_indices(self) -> list[int]:
        changed: list[int] = []
        counter = 0
        for row, item in enumerate(self._items):
            if item.checked:
                counter += 1
                new_idx = counter
            else:
                new_idx = -1
            if item.load_index != new_idx:
                item.load_index = new_idx
                changed.append(row)
        old_active = self._active_count
        self._active_count = counter
        if old_active != self._active_count:
            self.active_count_changed.emit()
        return changed

    @property
    def active_count(self) -> int:
        return self._active_count
