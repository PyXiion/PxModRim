from __future__ import annotations

from PySide6.QtCore import (
    QAbstractItemModel,
    QEvent,
    QItemSelection,
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
    QListView,
    QStyle,
    QStyledItemDelegate,
    QStyleOptionViewItem,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.models.metadata.structures import AboutXmlMod, ListedMod
from pxmodrim.ui.mod_list_model import ModFilterProxyModel, ModListModel
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
        # Get mod from model's custom role
        model = index.model()
        if not isinstance(model, ModFilterProxyModel):
            super().paint(painter, option, index)
            return

        source_model = model.sourceModel()
        if not isinstance(source_model, ModListModel):
            super().paint(painter, option, index)
            return

        mod: ListedMod | None = index.data(ModListModel.ModRole)
        if mod is None:
            super().paint(painter, option, index)
            return

        painter.save()
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        is_selected = bool(option.state & QStyle.StateFlag.State_Selected)
        is_active = index.data(ModListModel.CheckStateRole) == Qt.CheckState.Checked

        r = option.rect

        # Background
        if is_selected:
            painter.fillRect(r, QColor(56, 62, 68))
            painter.setPen(QPen(QColor(0, 162, 255), 3))
            painter.drawLine(r.left(), r.top(), r.left(), r.bottom())
        else:
            painter.fillRect(r, MAIN_BG_Q)

        cx = 0

        # Checkbox - custom drawing
        checkbox_size = 14
        checkbox_x = r.left() + MARGIN + (CHECKBOX_WIDTH - checkbox_size) // 2
        checkbox_y = r.top() + (r.height() - checkbox_size) // 2
        checkbox_rect = QRect(checkbox_x, checkbox_y, checkbox_size, checkbox_size)
        self._draw_checkbox(painter, checkbox_rect, is_active)
        cx = checkbox_rect.right() + 2

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
        if (
            isinstance(event, QMouseEvent)
            and event.button() == Qt.MouseButton.LeftButton
            and event.type() == QEvent.Type.MouseButtonRelease
        ):
            # Calculate checkbox rect same as in paint()
            checkbox_size = 14
            checkbox_x = option.rect.left() + MARGIN + (CHECKBOX_WIDTH - checkbox_size) // 2
            checkbox_y = option.rect.top() + (option.rect.height() - checkbox_size) // 2
            checkbox_rect = QRect(checkbox_x, checkbox_y, checkbox_size, checkbox_size)

            if checkbox_rect.contains(event.pos()):
                current = index.data(ModListModel.CheckStateRole)
                new_state = (
                    Qt.CheckState.Unchecked
                    if current == Qt.CheckState.Checked
                    else Qt.CheckState.Checked
                )
                return model.setData(index, new_state, ModListModel.CheckStateRole)
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
        self.search_input.textChanged.connect(self._on_search_changed)
        search_layout.addWidget(self.search_input)
        layout.addWidget(search_container)

        # Model/View setup
        self._model = ModListModel()
        self._proxy = ModFilterProxyModel(self._model)
        self._model.active_mods_changed.connect(self._on_model_active_changed)

        self.list = QListView()
        self.list.setObjectName("modListWidget")
        self.list.setModel(self._proxy)
        self.list.setItemDelegate(ModListDelegate(self.list))
        self.list.setSpacing(0)
        self.list.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
        self.list.setDragDropMode(QAbstractItemView.DragDropMode.InternalMove)
        self.list.setDefaultDropAction(Qt.DropAction.MoveAction)
        self.list.setUniformItemSizes(True)
        self.list.selectionModel().selectionChanged.connect(self._on_selection_changed)
        layout.addWidget(self.list)

    def load_mods(self, mods: dict[str, ListedMod], active_uuids: set[str]) -> None:
        self._model.load_mods(mods, active_uuids)

    def active_uuids(self) -> list[str]:
        return self._model.active_uuids()

    @property
    def proxy(self) -> ModFilterProxyModel:
        return self._proxy

    def _on_search_changed(self, text: str) -> None:
        self._proxy.set_search_filter(text)

    def _on_model_active_changed(self) -> None:
        self.active_mods_changed.emit()

    def _on_selection_changed(
        self, selected: QItemSelection, deselected: QItemSelection
    ) -> None:
        indexes = self.list.selectionModel().selectedIndexes()
        if indexes:
            index = self._proxy.mapToSource(indexes[0])
            mod = index.data(ModListModel.ModRole)
            if mod is not None:
                self.mod_selected.emit(mod)

    def _filter_items(self) -> None:
        # Kept for compatibility - no-op now
        pass