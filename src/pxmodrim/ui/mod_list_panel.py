from __future__ import annotations

from functools import partial

from PySide6.QtCore import (
    QAbstractItemModel,
    QEvent,
    QItemSelection,
    QItemSelectionModel,
    QModelIndex,
    QObject,
    QPersistentModelIndex,
    QPoint,
    QRect,
    QSize,
    Qt,
    QTimer,
    Signal,
)
from PySide6.QtGui import QBrush, QColor, QFont, QPainter, QPainterPath, QPen
from PySide6.QtWidgets import (
    QAbstractItemView,
    QHBoxLayout,
    QLineEdit,
    QListView,
    QStyle,
    QStyledItemDelegate,
    QStyleOptionViewItem,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.models.metadata.structures import AboutXmlMod, ListedMod
from pxmodrim.ui.mod_list_model import ModListModel
from pxmodrim.ui.palette import (
    BLUE_Q,
    BORDER_Q,
    CHECKBOX_BORDER_Q,
    CHECKBOX_CHECK_Q,
    CHECKBOX_FILL_Q,
    ITEM_ACTIVE_Q,
    ITEM_HOVER_Q,
    MAIN_BG_Q,
    TEXT_MAIN_Q,
    TEXT_MUTED_Q,
    WARNING_Q,
)

ROW_HEIGHT = 46
INDEX_WIDTH = 28
CHECKBOX_WIDTH = 22
MARGIN = 6


class ModListDelegate(QStyledItemDelegate):
    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._cb_hover_row: int | None = None

    def paint(
        self,
        painter: QPainter,
        option: QStyleOptionViewItem,
        index: QModelIndex | QPersistentModelIndex,
    ) -> None:
        model = index.model()
        if not index.isValid() or index.row() < 0 or index.row() >= model.rowCount():
            return

        if not isinstance(model, ModListModel):
            super().paint(painter, option, index)
            return

        mod = index.data(ModListModel.ModRole)
        if mod is None:
            super().paint(painter, option, index)
            return

        self.initStyleOption(option, index)
        painter.save()
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        r = option.rect
        clip = QPainterPath()
        clip.addRoundedRect(r, 8, 8)
        painter.setClipPath(clip)

        is_selected = bool(option.state & QStyle.StateFlag.State_Selected)
        is_hovered = bool(option.state & QStyle.StateFlag.State_MouseOver)
        is_active = index.data(ModListModel.CheckStateRole) == Qt.CheckState.Checked

        if is_selected:
            painter.fillRect(r, ITEM_ACTIVE_Q)
            painter.setPen(QPen(BLUE_Q, 3))
            painter.drawLine(r.left(), r.top(), r.left(), r.bottom())
        elif is_hovered:
            painter.fillRect(r, ITEM_HOVER_Q)
        else:
            painter.fillRect(r, MAIN_BG_Q)

        # ── Checkbox ──
        checkbox_size = 14
        checkbox_x = r.left() + MARGIN + (CHECKBOX_WIDTH - checkbox_size) // 2
        checkbox_y = r.top() + (r.height() - checkbox_size) // 2
        checkbox_rect = QRect(checkbox_x, checkbox_y, checkbox_size, checkbox_size)
        cb_hovered = index.row() == self._cb_hover_row
        self._draw_checkbox(painter, checkbox_rect, is_active, cb_hovered)
        cx = checkbox_rect.right() + 2

        # ── Index number ──
        idx_rect = QRect(cx, r.top(), INDEX_WIDTH, r.height())
        self._draw_index(painter, idx_rect, index.row())
        cx = idx_rect.right() + 4

        # ── Avatar (first letter of mod name) ──
        avatar_size = 32
        avatar_y = r.top() + (r.height() - avatar_size) // 2
        avatar_rect = QRect(cx, avatar_y, avatar_size, avatar_size)
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QBrush(BORDER_Q))
        painter.drawRoundedRect(avatar_rect, 4, 4)
        avatar_char = mod.name[0].upper() if mod.name else "?"
        avatar_font = QFont(painter.font())
        avatar_font.setPointSize(10)
        painter.setFont(avatar_font)
        painter.setPen(TEXT_MAIN_Q)
        painter.drawText(avatar_rect, Qt.AlignmentFlag.AlignCenter, avatar_char)
        cx = avatar_rect.right() + 12

        # ── Name (bold) ──
        font = painter.font()
        name_font = QFont(font)
        name_font.setBold(True)
        painter.setFont(name_font)

        is_incompat = (
            isinstance(mod, AboutXmlMod)
            and mod.mod_version
            and mod.mod_version == "1.4"
        )
        painter.setPen(WARNING_Q if is_incompat else TEXT_MAIN_Q)

        name_w = r.right() - cx - 80
        name_text = painter.fontMetrics().elidedText(
            mod.name, Qt.TextElideMode.ElideRight, name_w
        )
        painter.drawText(
            QRect(cx, r.top() + 4, name_w, 20),
            Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignBottom,
            name_text,
        )

        # ── Package ID (smaller, below name) ──
        sub_text = str(mod.package_id) if isinstance(mod, AboutXmlMod) else ""
        if sub_text:
            sub_font = QFont(font)
            sub_font.setPointSize(max(sub_font.pointSize() - 2, 8))
            sub_font.setFamilies(["monospace"])
            painter.setFont(sub_font)
            painter.setPen(TEXT_MUTED_Q)
            painter.drawText(
                QRect(cx, r.top() + 20, name_w, 18),
                Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop,
                sub_text,
            )

        # ── Version tag (right-aligned, blue-tinted) ──
        ver_text = (
            str(mod.mod_version)
            if isinstance(mod, AboutXmlMod) and mod.mod_version
            else ""
        )
        if ver_text:
            painter.setFont(font)
            tag_fm = painter.fontMetrics()
            tag_text_w = tag_fm.horizontalAdvance(ver_text)
            tag_w = tag_text_w + 16
            tag_h = 20
            tag_x = r.right() - tag_w - 10
            tag_y = r.top() + (r.height() - tag_h) // 2

            tag_bg = QColor(59, 130, 246, 51)
            painter.setBrush(QBrush(tag_bg))
            painter.setPen(Qt.PenStyle.NoPen)
            painter.drawRoundedRect(tag_x, tag_y, tag_w, tag_h, 4, 4)

            painter.setPen(QPen(BLUE_Q))
            tag_font = QFont(font)
            tag_font.setPointSize(max(tag_font.pointSize() - 2, 8))
            painter.setFont(tag_font)
            painter.drawText(
                QRect(tag_x, tag_y, tag_w, tag_h),
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
        return False

    def check_hit_test(self, viewport_pos: QPoint, viewport: QWidget) -> int | None:
        index = viewport.parent().indexAt(viewport_pos)
        if not index.isValid():
            return None
        option = viewport.parent().visualRect(index)
        checkbox_rect = QRect(
            option.left() + MARGIN + (CHECKBOX_WIDTH - 14) // 2,
            option.top() + (option.height() - 14) // 2,
            14,
            14,
        )
        return index.row() if checkbox_rect.contains(viewport_pos) else None

    def _draw_checkbox(
        self, painter: QPainter, rect: QRect, checked: bool, hovered: bool
    ) -> None:
        painter.save()
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)
        size = 14
        cx = rect.center().x() - size // 2
        cy = rect.center().y() - size // 2
        border = BLUE_Q if hovered else CHECKBOX_BORDER_Q
        fill = QColor(51, 65, 85, 80) if hovered else CHECKBOX_FILL_Q
        painter.setPen(QPen(border, 1))
        painter.setBrush(QBrush(fill))
        painter.drawRect(cx, cy, size, size)
        if checked:
            painter.setPen(QPen(CHECKBOX_CHECK_Q, 2))
            painter.drawLine(cx + 3, cy + 7, cx + 6, cy + 10)
            painter.drawLine(cx + 6, cy + 10, cx + 11, cy + 4)
        painter.restore()

    def _draw_index(self, painter: QPainter, rect: QRect, row: int) -> None:
        painter.setPen(TEXT_MUTED_Q)
        painter.drawText(rect, Qt.AlignmentFlag.AlignCenter, str(row + 1))

    def sizeHint(
        self,
        option: QStyleOptionViewItem,
        index: QModelIndex | QPersistentModelIndex,
    ) -> QSize:
        return QSize(200, ROW_HEIGHT)


class ModListPanel(QWidget):
    mod_selected = Signal(str)
    active_mods_changed = Signal()

    def __init__(self) -> None:
        super().__init__()
        self._cb_press_row: int | None = None
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        search_container = QWidget()
        search_container.setObjectName("searchBox")
        search_layout = QHBoxLayout(search_container)
        search_layout.setContentsMargins(12, 12, 12, 12)

        self.search_input = QLineEdit()
        self.search_input.setObjectName("searchInput")
        self.search_input.setPlaceholderText("Quick search by name or PackageID...")
        self.search_input.setClearButtonEnabled(True)
        self.search_input.textChanged.connect(self._on_search_changed)
        search_layout.addWidget(self.search_input)
        layout.addWidget(search_container)

        self._model = ModListModel()
        self._model.active_mods_changed.connect(self._on_model_active_changed)

        self.list = QListView()
        self.list.setObjectName("modListWidget")
        self.list.setModel(self._model)
        self.list.viewport().setMouseTracking(True)
        self._delegate = ModListDelegate(self.list)
        self.list.setItemDelegate(self._delegate)
        self.list.setSpacing(2)
        self.list.setVerticalScrollMode(QAbstractItemView.ScrollMode.ScrollPerPixel)
        self.list.setVerticalScrollMode(QAbstractItemView.ScrollMode.ScrollPerPixel)
        self.list.verticalScrollBar().setSingleStep(20)
        self.list.installEventFilter(self)
        self.list.viewport().installEventFilter(self)
        self.list.selectionModel().selectionChanged.connect(self._on_selection_changed)
        layout.addWidget(self.list)

    def load_mods(self, mods: dict[str, ListedMod], active_uuids: list[str]) -> None:
        self._model.load_mods(mods, active_uuids)

    def active_uuids(self) -> list[str]:
        return self._model.active_uuids()

    @property
    def proxy(self) -> ModListModel:
        return self._model

    @property
    def model(self) -> ModListModel:
        return self._model

    def _on_search_changed(self, text: str) -> None:
        self._model.set_search_filter(text)

    def _on_model_active_changed(self) -> None:
        self.active_mods_changed.emit()

    def _on_selection_changed(
        self, selected: QItemSelection, deselected: QItemSelection
    ) -> None:
        if self._model.signalsBlocked():
            return
        indexes = self.list.selectionModel().selectedIndexes()
        if not indexes:
            return

        current_index = indexes[0]
        if (
            current_index.isValid()
            and 0 <= current_index.row() < self._model.rowCount()
        ):
            uuid = current_index.data(ModListModel.UuidRole)
            if uuid:
                QTimer.singleShot(1, partial(self.mod_selected.emit, uuid))

    def eventFilter(self, watched: QObject, event: QEvent) -> bool:
        if watched is self.list.viewport() and event.type() == QEvent.Type.MouseMove:
            mouse_event = event
            row = self._delegate.check_hit_test(mouse_event.pos(), watched)
            if row != self._delegate._cb_hover_row:
                self._delegate._cb_hover_row = row
                self.list.viewport().update()
            return False
        if watched is self.list.viewport() and event.type() == QEvent.Type.Leave:
            if self._delegate._cb_hover_row is not None:
                self._delegate._cb_hover_row = None
                self.list.viewport().update()
            return False
        if (
            watched is self.list.viewport()
            and event.type() == QEvent.Type.MouseButtonPress
        ):
            mouse_event = event
            row = self._delegate.check_hit_test(mouse_event.pos(), watched)
            if row is not None:
                self._cb_press_row = row
                index = self._model.index(row, 0)
                current = self._model.data(index, ModListModel.CheckStateRole)
                new_state = (
                    Qt.CheckState.Unchecked
                    if current == Qt.CheckState.Checked
                    else Qt.CheckState.Checked
                )
                self._model.setData(index, new_state, ModListModel.CheckStateRole)
                return True
            return False
        if (
            watched is self.list.viewport()
            and event.type() == QEvent.Type.MouseButtonRelease
        ):
            if self._cb_press_row is not None:
                self._cb_press_row = None
                return True
            return False
        if watched is self.list and event.type() == QEvent.Type.Wheel:
            wheel_event = event
            scrollbar = self.list.verticalScrollBar()
            delta = wheel_event.angleDelta().y()
            scrollbar.setValue(scrollbar.value() - delta // 3)
            return True
        if watched is self.list and event.type() == QEvent.Type.KeyPress:
            key_event = event
            if key_event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter):
                selection_model = self.list.selectionModel()
                if selection_model:
                    selected_rows = selection_model.selectedRows()
                    if selected_rows:
                        rows: list[int] = []
                        checked = 0
                        for index in selected_rows:
                            if (
                                index.isValid()
                                and 0 <= index.row() < self._model.rowCount()
                            ):
                                rows.append(index.row())
                                if (
                                    self._model.data(index, ModListModel.CheckStateRole)
                                    == Qt.CheckState.Checked
                                ):
                                    checked += 1
                        if not rows:
                            return True
                        new_checked = checked < len(rows) - checked
                        self._model.set_check_states(rows, new_checked)
                        idxs = [self._model.index(r, 0) for r in rows]
                        selection = QItemSelection()
                        for idx in idxs:
                            selection.select(idx, idx)
                        selection_model.select(
                            selection,
                            QItemSelectionModel.SelectionFlag.ClearAndSelect
                            | QItemSelectionModel.SelectionFlag.Rows,
                        )
                        return True
        return super().eventFilter(watched, event)
