from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QObject, Qt, Signal, Slot
from PySide6.QtGui import QAction, QColor, QIcon
from PySide6.QtQml import QQmlEngine
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import QHBoxLayout, QLineEdit, QVBoxLayout, QWidget

from pxmodrim.models.metadata.structures import ListedMod
from pxmodrim.ui.components.icons import svg_str
from pxmodrim.ui.mod_list_model import ModListModel
from pxmodrim.ui.palette import PALETTE

_QML_DIR = Path(__file__).parent / "qml"
_MOD_LIST_QML = _QML_DIR / "ModList.qml"


class ModListPanel(QWidget):
    mod_selected = Signal(str)
    active_mods_changed = Signal()
    order_changed = Signal()

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

        # Model
        self._model = ModListModel(provider_colors)
        self._model.active_mods_changed.connect(self._on_model_active_changed)

        # QML view
        self._qml = QQuickWidget(qml_engine, self)  # type: ignore[arg-type]
        self._qml.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        self._qml.setAttribute(Qt.WidgetAttribute.WA_AlwaysStackOnTop, False)
        self._qml.setClearColor(QColor(PALETTE["ELEVATE_1"]))
        self._qml.setAcceptDrops(True)

        ctx = self._qml.engine().rootContext()
        ctx.setContextProperty("modListPanel", self)
        ctx.setContextProperty("modListModel", self._model)
        self._qml.setSource(str(_MOD_LIST_QML))

        layout.addWidget(self._qml)

    # ── Public API ────────────────────────────────────────────

    def load_mods(self, mods: dict[str, ListedMod], active_uuids: list[str]) -> None:
        self._model.load_mods(mods, active_uuids)

    def active_uuids(self) -> list[str]:
        return self._model.active_uuids()

    @property
    def model(self) -> ModListModel:
        return self._model

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

    @Slot(int, result=str)
    def uuidAt(self, row: int) -> str:
        item = self._model.get_item(row)
        return item.uuid if item else ""

    @Slot(int)
    def toggleCheck(self, row: int) -> None:
        idx = self._model.index(row, 0)
        current = self._model.data(idx, ModListModel.CheckStateRole)
        new_state = (
            Qt.CheckState.Unchecked
            if current == Qt.CheckState.Checked
            else Qt.CheckState.Checked
        )
        self._model.setData(idx, new_state, ModListModel.CheckStateRole)

    @Slot(int, result=bool)
    def isChecked(self, row: int) -> bool:
        item = self._model.get_item(row)
        return item.checked if item else False

    @Slot(list)
    def commitOrder(self, uuids: list[str]) -> None:
        self._model.commitOrder(uuids)
        self.order_changed.emit()

    @Slot(int, int)
    def moveRow(self, source_row: int, target_row: int) -> None:
        self._model.move_row(source_row, target_row)

    @Slot()
    def dragEnded(self) -> None:
        self.order_changed.emit()

    # ── Private slots ─────────────────────────────────────────

    def _on_search_changed(self, text: str) -> None:
        self._model.set_search_filter(text)

    def _on_model_active_changed(self) -> None:
        self.active_mods_changed.emit()
