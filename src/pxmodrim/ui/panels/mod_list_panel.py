from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QEvent, QObject, Qt, Signal, Slot
from PySide6.QtGui import QAction, QColor, QIcon
from PySide6.QtQml import QQmlEngine
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import QHBoxLayout, QLineEdit, QVBoxLayout, QWidget

from pxmodrim.core.models.metadata.structures import ListedMod
from pxmodrim.ui.components.icons import svg_str
from pxmodrim.ui.models.mod_list_model import ModListModel
from pxmodrim.ui.models.mod_list_proxy_model import ModListProxyModel
from pxmodrim.ui.theme.palette import PALETTE

_QML_DIR = Path(__file__).parent
_MOD_LIST_QML = _QML_DIR / "ModList.qml"


class ModListPanel(QWidget):
    mod_selected = Signal(str)
    active_mods_changed = Signal()
    order_changed = Signal()
    selection_changed = Signal(list)

    def __init__(
        self, provider_colors: dict[str, str], qml_engine: QQmlEngine | None = None
    ) -> None:
        super().__init__()
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        # Search bar
        search_container = QWidget()
        search_container.setObjectName("searchBox")
        search_container.setFixedHeight(56)
        search_layout = QHBoxLayout(search_container)
        search_layout.setContentsMargins(16, 8, 16, 8)

        self.search_input = QLineEdit()
        self.search_input.setObjectName("searchInput")
        self.search_input.setPlaceholderText("Quick search by name or PackageID...")
        self.search_input.setClearButtonEnabled(True)
        self.search_input.textChanged.connect(self._on_search_changed)

        # Leading search icon
        search_icon = QIcon()
        svg = svg_str("search", PALETTE["TEXT_DIM"])
        from PySide6.QtGui import QPainter, QPixmap
        from PySide6.QtSvg import QSvgRenderer
        from PySide6.QtXml import QDomDocument

        pm = QPixmap(16, 16)
        pm.fill(Qt.GlobalColor.transparent)
        p = QPainter(pm)
        doc = QDomDocument()
        doc.setContent(svg.encode())
        QSvgRenderer(doc.toByteArray()).render(p)
        p.end()
        search_icon.addPixmap(pm)
        search_action = QAction(search_icon, "", self)
        self.search_input.addAction(
            search_action, QLineEdit.ActionPosition.LeadingPosition
        )

        search_layout.addWidget(self.search_input)
        layout.addWidget(search_container)

        # Models: source + proxy
        self._model = ModListModel(provider_colors)
        self._proxy = ModListProxyModel(self._model)
        self._model.active_mods_changed.connect(self._on_model_active_changed)

        # QML view
        self._qml = QQuickWidget(qml_engine, self)  # type: ignore[arg-type]
        self._qml.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        self._qml.setAttribute(Qt.WidgetAttribute.WA_AlwaysStackOnTop, False)
        self._qml.setClearColor(QColor(PALETTE["ELEVATE_1"]))
        self._qml.setAcceptDrops(True)

        ctx = self._qml.rootContext()
        ctx.setContextProperty("modListPanel", self)
        ctx.setContextProperty("modListModel", self._proxy)
        self._qml.setSource(str(_MOD_LIST_QML))

        layout.addWidget(self._qml)

        # Expose search-focus state to QML for keyboard navigation gating
        ctx.setContextProperty("searchFocused", False)
        self.search_input.installEventFilter(self)

    # ── Public API ────────────────────────────────────────────

    def load_mods(self, mods: dict[str, ListedMod], active_uuids: list[str]) -> None:
        self._model.load_mods(mods, active_uuids)

    def active_uuids(self) -> list[str]:
        return self._model.active_uuids()

    @property
    def model(self) -> ModListModel:
        return self._model

    @property
    def proxy_model(self) -> ModListProxyModel:
        return self._proxy

    def set_sidebar_filter(self, uuids: set[str] | None) -> None:
        self._proxy.set_sidebar_filter(uuids)

    def set_search_filter(self, text: str) -> None:
        self._proxy.set_search_filter(text)

    # ── Slots called from QML ─────────────────────────────────

    @Slot(str)
    def modSelected(self, uuid: str) -> None:
        self.mod_selected.emit(uuid)

    @Slot()
    def clearSelection(self) -> None:
        root = self._qml.rootObject()
        if root is not None:
            list_view = root.findChild(QObject, "listView")
            if list_view is not None:
                list_view.setProperty("currentIndex", -1)
                list_view.setProperty("selectedIndices", [])

    def _proxy_to_source_row(self, proxy_row: int) -> int:
        proxy_index = self._proxy.index(proxy_row, 0)
        source_index = self._proxy.mapToSource(proxy_index)
        return source_index.row()

    @Slot(int, result=str)
    def uuidAt(self, row: int) -> str:
        source_row = self._proxy_to_source_row(row)
        item = self._model.get_item(source_row)
        return item.uuid if item else ""

    @Slot(int)
    def toggleCheck(self, row: int) -> None:
        source_row = self._proxy_to_source_row(row)
        idx = self._model.index(source_row, 0)
        current = self._model.data(idx, ModListModel.CheckStateRole)
        new_state = (
            Qt.CheckState.Unchecked
            if current == Qt.CheckState.Checked
            else Qt.CheckState.Checked
        )
        self._model.setData(idx, new_state, ModListModel.CheckStateRole)

    @Slot(list)
    def toggleChecked(self, rows: list[int]) -> None:
        if not rows:
            return
        changed: list[int] = []
        for proxy_row in rows:
            source_row = self._proxy_to_source_row(proxy_row)
            item = self._model.get_item(source_row)
            if item is None:
                continue
            new_state = not item.checked
            item.checked = new_state
            changed.append(source_row)
        if not changed:
            return
        top = self._model.index(min(changed), 0)
        bottom = self._model.index(max(changed), 0)
        self._model.dataChanged.emit(top, bottom, [ModListModel.CheckStateRole])
        self._model.active_mods_changed.emit()

    @Slot(int, result=bool)
    def isChecked(self, row: int) -> bool:
        source_row = self._proxy_to_source_row(row)
        item = self._model.get_item(source_row)
        return item.checked if item else False

    @Slot(list)
    def enableMods(self, uuids: list[str]) -> None:
        rows: list[int] = []
        for uuid in uuids:
            for i, item in enumerate(self._model._items):
                if item.uuid == uuid and not item.checked:
                    rows.append(i)
                    break
        if rows:
            self._model.set_check_states(rows, True)

    @Slot(list)
    def commitOrder(self, uuids: list[str]) -> None:
        self._model.commitOrder(uuids)
        self.order_changed.emit()

    @Slot(int, int)
    def moveRow(self, source_row: int, target_row: int) -> None:
        self._proxy.move_row(source_row, target_row)

    @Slot()
    def dragEnded(self) -> None:
        self.order_changed.emit()

    @Slot(list)
    def selectionChanged(self, rows: list[int]) -> None:
        uuids = [self.uuidAt(r) for r in rows]
        self.selection_changed.emit(uuids)

    # ── Private slots ─────────────────────────────────────────

    def _on_search_changed(self, text: str) -> None:
        self._proxy.set_search_filter(text)

    def _on_model_active_changed(self) -> None:
        self.active_mods_changed.emit()

    def eventFilter(self, obj: QObject, event: QEvent) -> bool:
        if obj is self.search_input:
            root_ctx = self._qml.rootContext()
            if event.type() == QEvent.Type.FocusIn:
                root_ctx.setContextProperty("searchFocused", True)
            elif event.type() == QEvent.Type.FocusOut:
                root_ctx.setContextProperty("searchFocused", False)
        return super().eventFilter(obj, event)
