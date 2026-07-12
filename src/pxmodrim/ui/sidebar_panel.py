from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import field

from PySide6.QtCore import QModelIndex, QPersistentModelIndex, QRect, QSize, Qt, Signal
from PySide6.QtGui import QBrush, QFont, QPainter
from PySide6.QtWidgets import (
    QAbstractItemView,
    QComboBox,
    QFrame,
    QListWidget,
    QListWidgetItem,
    QStyle,
    QStyledItemDelegate,
    QStyleOptionViewItem,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.models.metadata.structures import ListedMod
from pxmodrim.ui.palette import (
    ITEM_ACTIVE_Q,
    ITEM_HOVER_Q,
    PANEL_BG_Q,
    TEXT_MAIN_Q,
    TEXT_MUTED_Q,
    WARNING_Q,
)

ENTRY_ROLE = Qt.ItemDataRole.UserRole + 3


class SidebarEntry(ABC):
    """A filter entry in the sidebar. Knows which mods it represents."""

    visible_uuids: set[str] = field(default_factory=set)
    count: int = 0

    @property
    @abstractmethod
    def label(self) -> str: ...

    def refresh_count(self) -> None:
        self.count = len(self.visible_uuids)

    def update_uuids(
        self, all_mods: dict[str, ListedMod], active_uuids: set[str]
    ) -> None:
        """Override for entries that need re-computation on toggles (Active/Inactive)."""
        pass


class AllModsEntry(SidebarEntry):
    @property
    def label(self) -> str:
        return "All"


class ActiveModsEntry(SidebarEntry):
    @property
    def label(self) -> str:
        return "Active"

    def update_uuids(
        self, all_mods: dict[str, ListedMod], active_uuids: set[str]
    ) -> None:
        self.visible_uuids = set(active_uuids)
        self.refresh_count()


class InactiveModsEntry(SidebarEntry):
    @property
    def label(self) -> str:
        return "Inactive"

    def update_uuids(
        self, all_mods: dict[str, ListedMod], active_uuids: set[str]
    ) -> None:
        self.visible_uuids = set(all_mods) - set(active_uuids)
        self.refresh_count()


class ErrorModsEntry(SidebarEntry):
    @property
    def label(self) -> str:
        return "With errors"


class ProviderModsEntry(SidebarEntry):
    def __init__(self, provider_id: str, label: str, uuids: set[str]) -> None:
        super().__init__()
        self._pid = provider_id
        self._label = label
        self.visible_uuids = uuids
        self.refresh_count()

    @property
    def label(self) -> str:
        return self._label


LABEL_ROLE = Qt.ItemDataRole.UserRole + 1
COUNT_ROLE = Qt.ItemDataRole.UserRole + 2


class SidebarDelegate(QStyledItemDelegate):
    def paint(
        self,
        painter: QPainter,
        option: QStyleOptionViewItem,
        index: QModelIndex | QPersistentModelIndex,
    ) -> None:
        painter.save()
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        is_selected = bool(option.state & QStyle.StateFlag.State_Selected)
        is_hovered = bool(option.state & QStyle.StateFlag.State_MouseOver)
        r = option.rect

        bg = (
            ITEM_ACTIVE_Q
            if is_selected
            else (ITEM_HOVER_Q if is_hovered else PANEL_BG_Q)
        )
        painter.fillRect(r, bg)

        label: str = index.data(LABEL_ROLE) or ""
        count: int = index.data(COUNT_ROLE) or 0

        is_errors = "errors" in (index.data(Qt.ItemDataRole.UserRole) or "")

        text_color = WARNING_Q if is_errors else TEXT_MAIN_Q

        font = painter.font()
        name_font = QFont(font)
        name_font.setPointSize(12)
        painter.setFont(name_font)
        painter.setPen(text_color)
        text_rect = QRect(r.left() + 12, r.top(), r.width() - 60, r.height())
        painter.drawText(
            text_rect,
            Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter,
            label,
        )

        if count > 0 or is_errors:
            badge_text = str(count)
            badge_font = QFont(font)
            badge_font.setPointSize(10)
            painter.setFont(badge_font)
            badge_w = painter.fontMetrics().horizontalAdvance(badge_text) + 14
            badge_h = 20
            badge_x = r.right() - badge_w - 10
            badge_y = r.top() + (r.height() - badge_h) // 2

            painter.setBrush(QBrush(TEXT_MUTED_Q))
            painter.setPen(Qt.PenStyle.NoPen)
            painter.drawRoundedRect(badge_x, badge_y, badge_w, badge_h, 10, 10)

            painter.setPen(TEXT_MAIN_Q)
            painter.drawText(
                QRect(badge_x, badge_y, badge_w, badge_h),
                Qt.AlignmentFlag.AlignCenter,
                badge_text,
            )

        painter.restore()

    def sizeHint(
        self,
        option: QStyleOptionViewItem,
        index: QModelIndex | QPersistentModelIndex,
    ) -> QSize:
        return QSize(100, 36)


class SidebarPanel(QWidget):
    entry_selected = Signal(object)  # SidebarEntry

    def __init__(self) -> None:
        super().__init__()
        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 12, 12, 12)
        layout.setSpacing(8)

        self.preset_combo = QComboBox()
        self.preset_combo.setObjectName("presetCombo")
        self.preset_combo.addItem("Default")
        layout.addWidget(self.preset_combo)

        self.filter_list = QListWidget()
        self.filter_list.setObjectName("filterList")
        self.filter_list.setFrameShape(QFrame.Shape.NoFrame)
        self.filter_list.setSpacing(1)
        self.filter_list.viewport().setMouseTracking(True)
        self.filter_list.setVerticalScrollMode(
            QAbstractItemView.ScrollMode.ScrollPerPixel
        )
        self.filter_list.verticalScrollBar().setSingleStep(20)
        self.filter_list.setItemDelegate(SidebarDelegate(self.filter_list))
        self.filter_list.currentRowChanged.connect(self._emit_entry)
        layout.addWidget(self.filter_list)

        self._entries: list[SidebarEntry] = []

    def set_entries(self, entries: list[SidebarEntry]) -> None:
        self._entries = entries
        self.filter_list.clear()
        for entry in entries:
            item = QListWidgetItem()
            item.setData(ENTRY_ROLE, entry)
            item.setData(LABEL_ROLE, entry.label)
            item.setData(COUNT_ROLE, entry.count)
            item.setData(Qt.ItemDataRole.UserRole, entry.label.lower())
            self.filter_list.addItem(item)
        if self.filter_list.count() > 0:
            self.filter_list.setCurrentRow(0)

    def update_counts(self) -> None:
        for i in range(self.filter_list.count()):
            item = self.filter_list.item(i)
            if item is None:
                continue
            entry: SidebarEntry = item.data(ENTRY_ROLE)
            item.setData(COUNT_ROLE, entry.count)
        self.filter_list.viewport().update()

    def _emit_entry(self, row: int) -> None:
        if 0 <= row < len(self._entries):
            self.entry_selected.emit(self._entries[row])
