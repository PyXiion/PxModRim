from __future__ import annotations

from natsort import natsorted
from PySide6.QtCore import (
    QAbstractItemModel,
    QEvent,
    QModelIndex,
    QPersistentModelIndex,
    QRect,
    QSize,
    Qt,
    Signal,
)
from PySide6.QtGui import QBrush, QColor, QFont, QMouseEvent, QPainter, QPen
from PySide6.QtWidgets import (
    QAbstractItemView,
    QHBoxLayout,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QStyle,
    QStyledItemDelegate,
    QStyleOptionViewItem,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.models.metadata.structures import AboutXmlMod, ListedMod
from pxmodrim.ui.palette import (
    CHECKBOX_BORDER_Q,
    CHECKBOX_CHECK_Q,
    CHECKBOX_FILL_Q,
    GREEN_Q,
    MAIN_BG_Q,
    TAG_BG_Q,
    TEXT_MAIN_Q,
    TEXT_MUTED_Q,
    WARNING_Q,
)

ROW_HEIGHT = 44
INDEX_WIDTH = 28
CHECKBOX_WIDTH = 22
MARGIN = 6


class ModListDelegate(QStyledItemDelegate):
    def paint(
        self,
        painter: QPainter,
        option: QStyleOptionViewItem,
        index: QModelIndex | QPersistentModelIndex,
    ) -> None:
        mod: ListedMod | None = index.data(Qt.ItemDataRole.UserRole)
        if mod is None:
            super().paint(painter, option, index)
            return

        painter.save()
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        is_selected = bool(option.state & QStyle.StateFlag.State_Selected)
        is_active = index.data(Qt.ItemDataRole.CheckStateRole) == Qt.CheckState.Checked

        r = option.rect

        # Background
        if is_selected:
            painter.fillRect(r, QColor(56, 62, 68))
            painter.setPen(QPen(QColor(0, 162, 255), 3))
            painter.drawLine(r.left(), r.top(), r.left(), r.bottom())
        else:
            painter.fillRect(r, MAIN_BG_Q)

        cx = 0

        # Checkbox
        check_rect = QRect(r.left() + MARGIN, r.top(), CHECKBOX_WIDTH, r.height())
        self._draw_checkbox(painter, check_rect, is_active)
        cx = check_rect.right() + 2

        # Index
        idx_rect = QRect(cx, r.top(), INDEX_WIDTH, r.height())
        self._draw_index(painter, idx_rect, index.row())
        cx = idx_rect.right() + 4

        # Name
        name_x = cx
        name_w = r.right() - name_x - 80
        font = painter.font()
        name_font = QFont(font)
        name_font.setBold(True)
        painter.setFont(name_font)

        is_incompat = (
            isinstance(mod, AboutXmlMod)
            and mod.mod_version
            and mod.mod_version == "1.4"
        )
        if is_incompat:
            painter.setPen(WARNING_Q)
        else:
            painter.setPen(TEXT_MAIN_Q)

        name_text = painter.fontMetrics().elidedText(
            mod.name, Qt.TextElideMode.ElideRight, name_w
        )
        painter.drawText(
            QRect(name_x, r.top() + 4, name_w, 20),
            Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignBottom,
            name_text,
        )

        # Package ID below name
        sub_text = str(mod.package_id) if isinstance(mod, AboutXmlMod) else ""
        if sub_text:
            sub_font = QFont(font)
            sub_font.setPointSize(max(sub_font.pointSize() - 2, 8))
            sub_font.setFamilies(["monospace"])
            painter.setFont(sub_font)
            painter.setPen(TEXT_MUTED_Q)
            painter.drawText(
                QRect(name_x, r.top() + 20, name_w, 18),
                Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop,
                sub_text,
            )

        # Version tag
        ver_text = (
            str(mod.mod_version)
            if isinstance(mod, AboutXmlMod) and mod.mod_version
            else ""
        )
        if ver_text:
            ver_x = r.right() - 74
            ver_y = r.top() + (r.height() - 18) // 2
            painter.setBrush(QBrush(TAG_BG_Q))
            painter.setPen(Qt.PenStyle.NoPen)
            painter.drawRoundedRect(ver_x, ver_y, 68, 18, 3, 3)
            painter.setPen(QPen(GREEN_Q))
            ver_font = QFont(font)
            ver_font.setPointSize(max(ver_font.pointSize() - 2, 8))
            painter.setFont(ver_font)
            painter.drawText(
                QRect(ver_x, ver_y, 68, 18),
                Qt.AlignmentFlag.AlignCenter,
                ver_text,
            )

        painter.restore()

    def editorEvent(
        self,
        event: QEvent,
        model: QAbstractItemModel,
        option: QStyleOptionViewItem,
        index: QModelIndex | QPersistentModelIndex,
    ) -> bool:
        if isinstance(event, QMouseEvent):
            if (
                event.button() == Qt.MouseButton.LeftButton
                and event.type() == QEvent.Type.MouseButtonRelease
            ):
                check_rect = QRect(
                    option.rect.left() + MARGIN,
                    option.rect.top(),
                    CHECKBOX_WIDTH,
                    option.rect.height(),
                )
                if check_rect.contains(event.pos()):
                    current = index.data(Qt.ItemDataRole.CheckStateRole)
                    new_state = (
                        Qt.CheckState.Unchecked
                        if current == Qt.CheckState.Checked
                        else Qt.CheckState.Checked
                    )
                    return model.setData(
                        index, new_state, Qt.ItemDataRole.CheckStateRole
                    )
        return super().editorEvent(event, model, option, index)

    def _draw_checkbox(self, painter: QPainter, rect: QRect, checked: bool) -> None:
        painter.save()
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)

        size = 14
        cx = rect.center().x() - size // 2
        cy = rect.center().y() - size // 2

        painter.setPen(QPen(CHECKBOX_BORDER_Q, 1))
        painter.setBrush(QBrush(CHECKBOX_FILL_Q))
        painter.drawRect(cx, cy, size, size)

        if checked:
            painter.setPen(QPen(CHECKBOX_CHECK_Q, 2))
            painter.drawLine(cx + 3, cy + 7, cx + 6, cy + 10)
            painter.drawLine(cx + 6, cy + 10, cx + 11, cy + 4)

        painter.restore()

    def _draw_index(self, painter: QPainter, rect: QRect, row: int) -> None:
        painter.setPen(TEXT_MUTED_Q)
        painter.drawText(
            rect,
            Qt.AlignmentFlag.AlignCenter,
            str(row + 1),
        )

    def sizeHint(
        self,
        option: QStyleOptionViewItem,
        index: QModelIndex | QPersistentModelIndex,
    ) -> QSize:
        return QSize(200, ROW_HEIGHT)


class ModListPanel(QWidget):
    mod_selected = Signal(object)
    active_mods_changed = Signal()

    def __init__(self) -> None:
        super().__init__()
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        # Search box container
        search_container = QWidget()
        search_container.setObjectName("searchBox")
        search_layout = QHBoxLayout(search_container)
        search_layout.setContentsMargins(12, 12, 12, 12)

        self.search_input = QLineEdit()
        self.search_input.setObjectName("searchInput")
        self.search_input.setPlaceholderText("Quick search by name or PackageID...")
        self.search_input.setClearButtonEnabled(True)
        self.search_input.textChanged.connect(self._filter_items)
        search_layout.addWidget(self.search_input)
        layout.addWidget(search_container)

        self.list = QListWidget()
        self.list.setObjectName("modListWidget")
        self.list.setItemDelegate(ModListDelegate(self.list))
        self.list.setSpacing(0)
        self.list.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
        self.list.setDragDropMode(QAbstractItemView.DragDropMode.InternalMove)
        self.list.setDefaultDropAction(Qt.DropAction.MoveAction)
        self.list.currentItemChanged.connect(self._on_selection)
        self.list.itemChanged.connect(self._on_item_changed)
        layout.addWidget(self.list)

        self._all_mods: dict[str, ListedMod] = {}
        self._visible_uuids: set[str] = set()

    @property
    def all_mods(self) -> dict[str, ListedMod]:
        return dict(self._all_mods)

    def load_mods(self, mods: dict[str, ListedMod], active_uuids: set[str]) -> None:
        self._all_mods = dict(mods)
        self._visible_uuids = set(mods.keys())

        sorted_uuids = [
            uuid
            for uuid, _ in natsorted(
                mods.items(),
                key=lambda kv: kv[1].name.lower(),
            )
        ]

        active_list: list[str] = []
        inactive_list: list[str] = []
        for uuid in sorted_uuids:
            if uuid in active_uuids:
                active_list.append(uuid)
            else:
                inactive_list.append(uuid)

        self.list.blockSignals(True)
        self.list.clear()
        for uuid in [*active_list, *inactive_list]:
            mod = mods[uuid]
            item = QListWidgetItem()
            item.setData(Qt.ItemDataRole.UserRole, mod)
            item.setFlags(item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
            item.setCheckState(
                Qt.CheckState.Checked
                if uuid in active_uuids
                else Qt.CheckState.Unchecked
            )
            self.list.addItem(item)
        self.list.blockSignals(False)
        self._filter_items()

    def active_uuids(self) -> list[str]:
        result: list[str] = []
        for i in range(self.list.count()):
            item = self.list.item(i)
            if item is None:
                continue
            if item.checkState() == Qt.CheckState.Checked:
                mod: ListedMod | None = item.data(Qt.ItemDataRole.UserRole)
                if mod is not None:
                    result.append(mod.uuid)
        return result

    def show_uuids(self, uuids: set[str]) -> None:
        self._visible_uuids = uuids
        self._filter_items()

    def _filter_items(self) -> None:
        search = self.search_input.text().lower()

        for i in range(self.list.count()):
            item = self.list.item(i)
            if item is None:
                continue
            mod: ListedMod | None = item.data(Qt.ItemDataRole.UserRole)
            if mod is None:
                item.setHidden(True)
                continue

            visible = mod.uuid in self._visible_uuids

            if visible and search:
                name_match = search in mod.name.lower()
                pid_match = (
                    isinstance(mod, AboutXmlMod)
                    and search in str(mod.package_id).lower()
                )
                if not name_match and not pid_match:
                    visible = False

            item.setHidden(not visible)

    def _on_selection(
        self, current: QListWidgetItem | None, _previous: QListWidgetItem | None
    ) -> None:
        if current is not None and not current.isHidden():
            mod = current.data(Qt.ItemDataRole.UserRole)
            if mod is not None:
                self.mod_selected.emit(mod)

    def _on_item_changed(self, item: QListWidgetItem | None) -> None:
        if item is not None:
            self.active_mods_changed.emit()
            self._filter_items()
