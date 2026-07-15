from __future__ import annotations

from collections.abc import Sequence
from typing import Any

from PySide6.QtCore import (
    QAbstractListModel,
    QByteArray,
    QModelIndex,
    QObject,
    QPersistentModelIndex,
    Qt,
    Signal,
)

from pxmodrim.ui.theme.palette import PALETTE

# (badge_bg, badge_fg, icon_name, icon_color)
_ENTRY_TYPES: dict[str, tuple[str, str, str, str]] = {
    "all": (PALETTE["ELEVATE_4"], PALETTE["TEXT_MAIN"], "grid", PALETTE["TEXT_DIM"]),
    "active": (
        PALETTE["SUCCESS_BG"],
        PALETTE["SUCCESS"],
        "check-circle",
        PALETTE["SUCCESS"],
    ),
    "inactive": (
        PALETTE["ELEVATE_4"],
        PALETTE["TEXT_MAIN"],
        "x-circle",
        PALETTE["TEXT_DIM"],
    ),
    "errors": (PALETTE["DANGER_BG"], PALETTE["DANGER"], "error", PALETTE["DANGER"]),
    "warnings": (
        PALETTE["WARNING_BG"],
        PALETTE["WARNING"],
        "warning",
        PALETTE["WARNING"],
    ),
    "provider": (
        PALETTE["ELEVATE_4"],
        PALETTE["TEXT_MAIN"],
        "folder",
        PALETTE["TEXT_DIM"],
    ),
}

_PROVIDER_ICON_COLORS: dict[str, str] = {
    "steam": PALETTE["SUCCESS"],
    "steam_cmd": PALETTE["SUCCESS"],
    "local": PALETTE["WARNING"],
}

# Section headers by position
_SECTION_MAP: dict[int, str] = {
    0: "Library",
}

# Provider icon overrides
_PROVIDER_ICONS: dict[str, str] = {
    "steam": "steam",
    "steam_cmd": "steam",
    "local": "folder",
    "core": "grid",
    "git": "git",
}


class SidebarItem:
    __slots__ = (
        "label",
        "count",
        "badge_bg",
        "badge_fg",
        "icon_name",
        "icon_color",
        "section_name",
        "entry",
    )

    def __init__(
        self,
        label: str,
        count: int,
        badge_bg: str,
        badge_fg: str,
        icon_name: str,
        icon_color: str,
        section_name: str,
        entry: object,
    ) -> None:
        self.label = label
        self.count = count
        self.badge_bg = badge_bg
        self.badge_fg = badge_fg
        self.icon_name = icon_name
        self.icon_color = icon_color
        self.section_name = section_name
        self.entry = entry


def _detect_entry_type(entry: object) -> str:
    cls = type(entry).__name__
    if "Active" in cls and "Inactive" not in cls:
        return "active"
    if "Inactive" in cls:
        return "inactive"
    if "Error" in cls:
        return "errors"
    if "Warning" in cls:
        return "warnings"
    if "All" in cls:
        return "all"
    return "provider"


def _section_for_index(idx: int) -> str:
    if idx < 3:
        return "Library"
    if idx < 5:
        return "Diagnostics"
    return "Providers"


def _icon_for_provider(entry: object) -> str:
    """Determine provider icon from entry data."""
    label = getattr(entry, "label", "").lower()
    for keyword, icon_name in _PROVIDER_ICONS.items():
        if keyword in label:
            return icon_name
    return "folder"


class SidebarModel(QAbstractListModel):
    LabelRole = Qt.ItemDataRole.UserRole + 1
    CountRole = Qt.ItemDataRole.UserRole + 2
    BadgeBgRole = Qt.ItemDataRole.UserRole + 3
    BadgeFgRole = Qt.ItemDataRole.UserRole + 4
    IconRole = Qt.ItemDataRole.UserRole + 5
    IconColorRole = Qt.ItemDataRole.UserRole + 6
    SectionRole = Qt.ItemDataRole.UserRole + 7

    entry_selected = Signal(int)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._items: list[SidebarItem] = []

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
        if not index.isValid() or index.row() >= len(self._items):
            return None

        item = self._items[index.row()]

        if role == Qt.ItemDataRole.DisplayRole or role == self.LabelRole:
            return item.label if item.label else ""
        if role == self.CountRole:
            return item.count if item.count is not None else 0
        if role == self.BadgeBgRole:
            return item.badge_bg if item.badge_bg else ""
        if role == self.BadgeFgRole:
            return item.badge_fg if item.badge_fg else ""
        if role == self.IconRole:
            return item.icon_name if item.icon_name else ""
        if role == self.IconColorRole:
            return item.icon_color if item.icon_color else ""
        if role == self.SectionRole:
            return item.section_name if item.section_name else ""

        return None

    def roleNames(self) -> dict[int, QByteArray]:
        return {
            Qt.ItemDataRole.DisplayRole: QByteArray(b"label"),
            self.LabelRole: QByteArray(b"label"),
            self.CountRole: QByteArray(b"count"),
            self.BadgeBgRole: QByteArray(b"badgeBg"),
            self.BadgeFgRole: QByteArray(b"badgeFg"),
            self.IconRole: QByteArray(b"iconName"),
            self.IconColorRole: QByteArray(b"iconColor"),
            self.SectionRole: QByteArray(b"sectionName"),
        }

    def set_entries(self, entries: Sequence[object]) -> None:
        self.beginResetModel()
        self._items = []
        for i, e in enumerate(entries):
            entry_type = _detect_entry_type(e)
            bg, fg, icon_name, icon_color = _ENTRY_TYPES.get(
                entry_type, _ENTRY_TYPES["provider"]
            )
            if entry_type == "provider":
                icon_name = _icon_for_provider(e)
                label_lower = getattr(e, "label", "").lower()
                if "steam" in label_lower:
                    icon_color = _PROVIDER_ICON_COLORS.get("steam", icon_color)
                elif "local" in label_lower:
                    icon_color = _PROVIDER_ICON_COLORS.get("local", icon_color)
            section = _section_for_index(i)
            self._items.append(
                SidebarItem(
                    label=getattr(e, "label", ""),
                    count=getattr(e, "count", 0),
                    badge_bg=bg,
                    badge_fg=fg,
                    icon_name=icon_name,
                    icon_color=icon_color,
                    section_name=section,
                    entry=e,
                )
            )
        self.endResetModel()

    def update_entries(self, entries: Sequence[object]) -> None:
        if len(entries) != len(self._items):
            self.set_entries(entries)
            return

        changed_roles: set[int] = set()
        for i, entry in enumerate(entries):
            item = self._items[i]
            entry_type = _detect_entry_type(entry)
            bg, fg, _, icon_color = _ENTRY_TYPES.get(
                entry_type, _ENTRY_TYPES["provider"]
            )
            if entry_type == "provider":
                label_lower = getattr(entry, "label", "").lower()
                if "steam" in label_lower:
                    icon_color = _PROVIDER_ICON_COLORS.get("steam", icon_color)
                elif "local" in label_lower:
                    icon_color = _PROVIDER_ICON_COLORS.get("local", icon_color)

            new_label = getattr(entry, "label", "") or ""
            new_count = getattr(entry, "count", 0)

            if item.label != new_label:
                item.label = new_label
                changed_roles.add(self.LabelRole)
            if item.count != new_count:
                item.count = new_count
                changed_roles.add(self.CountRole)
            if item.badge_bg != bg:
                item.badge_bg = bg
                changed_roles.add(self.BadgeBgRole)
            if item.badge_fg != fg:
                item.badge_fg = fg
                changed_roles.add(self.BadgeFgRole)

            item.entry = entry

        if changed_roles and self._items:
            top = self.index(0, 0)
            bottom = self.index(len(self._items) - 1, 0)
            self.dataChanged.emit(top, bottom, list(changed_roles))

    def get_entry(self, row: int) -> object | None:
        if 0 <= row < len(self._items):
            return self._items[row].entry
        return None
