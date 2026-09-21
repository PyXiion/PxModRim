from __future__ import annotations

import asyncio
from pathlib import Path

from PySide6.QtCore import QEvent, QObject, Qt, Signal, Slot
from PySide6.QtGui import QAction, QColor, QIcon
from PySide6.QtQml import QQmlEngine
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import (
    QHBoxLayout,
    QLineEdit,
    QMessageBox,
    QVBoxLayout,
    QWidget,
)
from qasync import asyncSlot

from pxmodrim.core.checker.graph import EdgeType, PackageId
from pxmodrim.core.context import CoreContext
from pxmodrim.core.models.metadata.structures import AboutXmlMod, ListedMod
from pxmodrim.ui.components.dialogs import await_dialog
from pxmodrim.ui.components.icons import svg_str
from pxmodrim.ui.models.mod_list_model import ModListModel
from pxmodrim.ui.models.mod_list_proxy_model import ModListProxyModel
from pxmodrim.ui.theme.palette import PALETTE

_QML_DIR = Path(__file__).parent
_MOD_LIST_QML = _QML_DIR / "ModList.qml"


class _DependentModsDialog(QMessageBox):
    def __init__(self, dependents: list[str], parent: QWidget) -> None:
        super().__init__(parent)
        self.setIcon(QMessageBox.Icon.Warning)
        self.setWindowTitle("Disable dependent mods?")
        self.setText(
            "These active mods depend on the mod you are disabling:\n\n"
            + "\n".join(f"• {dependent}" for dependent in dependents)
        )
        self.setInformativeText(
            "Choose whether to disable only the selected mod or these dependents too."
        )
        self.setStandardButtons(
            QMessageBox.StandardButton.Yes
            | QMessageBox.StandardButton.No
            | QMessageBox.StandardButton.Cancel
        )
        self.setDefaultButton(QMessageBox.StandardButton.No)
        self.setButtonText(
            QMessageBox.StandardButton.Yes, "Disable selected + dependents"
        )
        self.setButtonText(QMessageBox.StandardButton.No, "Disable selected only")
        self.setButtonText(QMessageBox.StandardButton.Cancel, "Cancel")


class ModListPanel(QWidget):
    mod_selected = Signal(str)
    active_mods_changed = Signal()
    order_changed = Signal()
    selection_changed = Signal(list)

    def __init__(self, ctx: CoreContext, qml_engine: QQmlEngine | None = None) -> None:
        super().__init__()
        self._ctx = ctx
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
        self._model = ModListModel({}, ctx.diagnostics_service)
        self._proxy = ModListProxyModel(self._model)
        self._model.active_mods_changed.connect(self.active_mods_changed)

        # QML view
        self._qml = QQuickWidget(qml_engine, self)  # type: ignore[arg-type]
        self._qml.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        self._qml.setAttribute(Qt.WidgetAttribute.WA_AlwaysStackOnTop, False)
        self._qml.setClearColor(QColor(PALETTE["ELEVATE_1"]))
        self._qml.setAcceptDrops(True)

        qml_ctx = self._qml.rootContext()
        qml_ctx.setContextProperty("modListPanel", self)
        qml_ctx.setContextProperty("modListModel", self._proxy)
        self._qml.setSource(str(_MOD_LIST_QML))

        layout.addWidget(self._qml)

        # Expose search-focus state to QML for keyboard navigation gating
        qml_ctx.setContextProperty("searchFocused", False)
        self.search_input.installEventFilter(self)

        ctx.active_state_changed.connect(self._on_core_state_changed)

        # Reactive bindings
        ctx.mod_service.mods_changed.connect(self._on_mods_changed)

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

    # ── Reactive ──────────────────────────────────────────────

    def _on_mods_changed(self, _: None = None) -> None:
        asyncio.ensure_future(self._refresh())

    async def _refresh(self) -> None:
        self._model.update_provider_colors(self._ctx.mod_service.provider_colors)
        self._model.load_mods(self._ctx.all_mods, self._ctx.active_uuids)
        impacts = await self._ctx.mod_service.startup_impact.get_all_averages()
        self._model.set_startup_impact(impacts)

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

    @asyncSlot(int)
    async def toggleCheck(self, row: int) -> None:
        await self._toggle_rows([row])

    @asyncSlot(list)
    async def toggleChecked(self, rows: list[int]) -> None:
        await self._toggle_rows(rows)

    async def _toggle_rows(self, rows: list[int]) -> None:
        if not rows:
            return

        source_rows: list[int] = []
        selected_uuids: list[str] = []
        for proxy_row in rows:
            source_row = self._proxy_to_source_row(proxy_row)
            item = self._model.get_item(source_row)
            if item is not None:
                source_rows.append(source_row)
                selected_uuids.append(item.uuid)
        if not selected_uuids:
            return

        active_uuids = self._model.active_uuids()
        disabling_uuids = {uuid for uuid in selected_uuids if uuid in active_uuids}
        affected_uuids = set(self._active_dependent_uuids(disabling_uuids)) - set(
            selected_uuids
        )
        disable_all = False
        if affected_uuids:
            affected_details = [
                self._dependent_detail(uuid)
                for uuid in active_uuids
                if uuid in affected_uuids
            ]
            result, _ = await await_dialog(_DependentModsDialog, affected_details, self)
            if result == QMessageBox.StandardButton.Cancel:
                return
            disable_all = result == QMessageBox.StandardButton.Yes

        changed_rows: list[int] = []
        for source_row in source_rows:
            item = self._model.get_item(source_row)
            if item is None:
                continue
            new_checked = False if item.uuid in disabling_uuids else not item.checked
            if item.checked != new_checked:
                item.checked = new_checked
                changed_rows.append(source_row)

        if disable_all:
            for source_row in range(self._model.rowCount()):
                item = self._model.get_item(source_row)
                if item is not None and item.uuid in affected_uuids and item.checked:
                    item.checked = False
                    changed_rows.append(source_row)
        if not changed_rows:
            return
        top = self._model.index(min(changed_rows), 0)
        bottom = self._model.index(max(changed_rows), 0)
        self._model.dataChanged.emit(top, bottom, [ModListModel.CheckStateRole])
        self._model.active_mods_changed.emit()
        self._ctx.set_active(self._model.active_uuids())

    def _active_dependent_uuids(self, uuids: set[str]) -> list[str]:
        graph = self._ctx.diagnostics_service.constraint_graph
        active_uuids = self._model.active_uuids()
        all_mods = self._ctx.all_mods
        roots: set[PackageId] = set()
        for uuid in uuids:
            mod = all_mods.get(uuid)
            if isinstance(mod, AboutXmlMod):
                roots.add(PackageId(str(mod.package_id)))

        visited = set(roots)
        pending = list(roots)
        while pending:
            target = pending.pop()
            for edge in graph.incoming_of_type(target, EdgeType.DEPENDENCY):
                if edge.source not in visited:
                    visited.add(edge.source)
                    pending.append(edge.source)

        uuid_by_pid: dict[str, str] = {}
        for uuid in active_uuids:
            mod = all_mods.get(uuid)
            if isinstance(mod, AboutXmlMod):
                uuid_by_pid[str(mod.package_id)] = uuid
        return [
            uuid_by_pid[str(pid)]
            for pid in visited
            if str(pid) in uuid_by_pid and uuid_by_pid[str(pid)] not in uuids
        ]

    def _dependent_detail(self, uuid: str) -> str:
        mod = self._ctx.all_mods[uuid]
        if isinstance(mod, AboutXmlMod):
            return f"{mod.name} ({mod.package_id})"
        return mod.name

    @Slot(int, result=bool)
    def isChecked(self, row: int) -> bool:
        source_row = self._proxy_to_source_row(row)
        item = self._model.get_item(source_row)
        return item.checked if item else False

    def _on_core_state_changed(self, active_uuids: object) -> None:
        if not isinstance(active_uuids, tuple):
            return
        current = frozenset(self._model.active_uuids())
        new = frozenset(active_uuids)
        self._model.set_checkable(new - current, True)
        self._model.set_checkable(current - new, False)
        if list(active_uuids) != self._model.active_uuids():
            self._model.commitOrder(list(active_uuids))

    @Slot(list)
    def commitOrder(self, uuids: list[str]) -> None:
        self._model.commitOrder(uuids)
        self._ctx.set_active(self._model.active_uuids())
        self.order_changed.emit()

    @Slot(int, int)
    def moveRow(self, source_row: int, target_row: int) -> None:
        self._proxy.move_row(source_row, target_row)

    @Slot()
    def dragEnded(self) -> None:
        self._ctx.set_active(self._model.active_uuids())
        self.order_changed.emit()

    @Slot(list)
    def selectionChanged(self, rows: list[int]) -> None:
        uuids = [self.uuidAt(r) for r in rows]
        self.selection_changed.emit(uuids)

    # ── Private slots ─────────────────────────────────────────

    def _on_search_changed(self, text: str) -> None:
        self._proxy.set_search_filter(text)

    def eventFilter(self, obj: QObject, event: QEvent) -> bool:
        if obj is self.search_input:
            root_ctx = self._qml.rootContext()
            if event.type() == QEvent.Type.FocusIn:
                root_ctx.setContextProperty("searchFocused", True)
            elif event.type() == QEvent.Type.FocusOut:
                root_ctx.setContextProperty("searchFocused", False)
        return super().eventFilter(obj, event)
