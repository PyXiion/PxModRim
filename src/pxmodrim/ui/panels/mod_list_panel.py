from __future__ import annotations

import asyncio
from pathlib import Path
from typing import TYPE_CHECKING, Any

from PySide6.QtCore import (
    Q_ARG,
    Property,
    QEvent,
    QMetaObject,
    QObject,
    Qt,
    QUrl,
    Signal,
    Slot,
)
from PySide6.QtGui import QAction, QColor
from PySide6.QtQml import QQmlEngine
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import (
    QHBoxLayout,
    QLineEdit,
    QMenu,
    QVBoxLayout,
    QWidget,
)
from qasync import asyncSlot

from pxmodrim.core.context import CoreContext
from pxmodrim.core.models.metadata.structures import ListedMod
from pxmodrim.ui.components.icons import icon
from pxmodrim.ui.components.mod_activation import toggle_mods
from pxmodrim.ui.components.mod_updates import (
    add_update_action,
    update_mods,
)
from pxmodrim.ui.models.mod_list_model import ModListModel
from pxmodrim.ui.models.mod_list_proxy_model import ModListProxyModel
from pxmodrim.ui.theme.palette import PALETTE

if TYPE_CHECKING:
    from pxmodrim.core.config import AppConfig
_QML_DIR = Path(__file__).parent
_MOD_LIST_QML = _QML_DIR / "ModList.qml"

_LIST_VIEW_CONTAIN = 4


class ModListPanel(QWidget):
    mod_selected = Signal(str)
    active_mods_changed = Signal()
    order_changed = Signal()
    selection_changed = Signal(list)
    compact_mode_changed = Signal(bool)
    highlighted_uuids_changed = Signal()

    @Property(bool, notify=compact_mode_changed)
    def compactMode(self) -> bool:
        return self._compact_mode

    @Slot(bool)
    def setCompactMode(self, enabled: bool) -> None:
        if self._compact_mode != enabled:
            self._compact_mode = enabled
            self.compact_mode_changed.emit(enabled)

    @Property(list, notify=highlighted_uuids_changed)
    def highlightedUuids(self) -> list[str]:
        return self._highlighted_uuids

    def __init__(
        self,
        ctx: CoreContext,
        qml_engine: QQmlEngine | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._ctx = ctx
        cfg = getattr(ctx, "config", None)
        self._compact_mode: bool = (
            getattr(cfg, "compact_mod_list", False) if cfg else False
        )
        self._highlighted_uuids: list[str] = []
        self._highlight_generation = 0
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        # Search bar
        search_container = QWidget(self)
        search_container.setObjectName("searchBox")
        search_container.setFixedHeight(56)
        search_layout = QHBoxLayout(search_container)
        search_layout.setContentsMargins(16, 8, 16, 8)

        self.search_input = QLineEdit(search_container)
        self.search_input.setObjectName("searchInput")
        self.search_input.setPlaceholderText("Search mods, package ID, author…")
        self.search_input.setClearButtonEnabled(True)
        self.search_input.textChanged.connect(self._on_search_changed)

        # Leading search icon
        search_icon = icon("search", 16, PALETTE["TEXT_DIM"])
        search_action = QAction(search_icon, "", self)
        self.search_input.addAction(
            search_action, QLineEdit.ActionPosition.LeadingPosition
        )

        search_layout.addWidget(self.search_input)
        layout.addWidget(search_container)

        # Models: source + proxy
        self._model = ModListModel({}, ctx.diagnostics_service, self)
        self._proxy = ModListProxyModel(self._model, self)
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
        qml_ctx.setContextProperty("modListHasFocus", False)
        self._qml.setSource(QUrl.fromLocalFile(str(_MOD_LIST_QML)))

        layout.addWidget(self._qml)

        self._qml.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self._qml.installEventFilter(self)

        ctx.active_state_changed.connect(self._on_core_state_changed)
        if hasattr(ctx, "config_changed"):
            ctx.config_changed.connect(self._on_config_changed)
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

    def select_uuid(self, uuid: str) -> bool:
        """Select and scroll to a mod; False if it is hidden by a filter."""
        row = self.rowForUuid(uuid)
        root: Any = self._qml.rootObject()
        if row < 0 or root is None:
            return False
        root.revealRow(row, uuid)
        return True

    @Slot()
    def clearSelection(self) -> None:
        root = self._qml.rootObject()
        if root is not None:
            list_view = root.findChild(QObject, "listView")
            if list_view is not None:
                list_view.setProperty("currentIndex", -1)
                list_view.setProperty("anchorIndex", -1)
                list_view.setProperty("selectedIndices", [])
                list_view.setProperty("currentUuid", "")
                list_view.setProperty("anchorUuid", "")
                list_view.setProperty("selectedUuids", [])
        self.selection_changed.emit([])
        self.mod_selected.emit("")

    def _proxy_to_source_row(self, proxy_row: int) -> int:
        proxy_index = self._proxy.index(proxy_row, 0)
        source_index = self._proxy.mapToSource(proxy_index)
        return source_index.row()

    @Slot(int, result=str)
    def uuidAt(self, row: int) -> str:
        source_row = self._proxy_to_source_row(row)
        item = self._model.get_item(source_row)
        return item.uuid if item else ""

    @Slot(str, result=int)
    def rowForUuid(self, uuid: str) -> int:
        row = self._proxy.proxy_row_for_uuid(uuid)
        return row if row is not None else -1

    @asyncSlot(int)
    async def toggleCheck(self, row: int) -> None:
        await self._toggle_rows([row])

    @asyncSlot(list)
    async def toggleChecked(self, rows: list[int]) -> None:
        await self._toggle_rows(rows)

    async def _toggle_rows(self, rows: list[int]) -> None:
        selected_uuids: list[str] = []
        for proxy_row in rows:
            item = self._model.get_item(self._proxy_to_source_row(proxy_row))
            if item is not None:
                selected_uuids.append(item.uuid)
        if not selected_uuids:
            return

        previously_active = set(self._ctx.active_uuids)
        await toggle_mods(self._ctx, self, selected_uuids)
        active_after = set(self._ctx.active_uuids)
        newly_enabled = [
            uuid
            for uuid in selected_uuids
            if uuid not in previously_active and uuid in active_after
        ]
        if newly_enabled:
            self._highlight_rows(newly_enabled)

    def _highlight_rows(self, uuids: list[str]) -> None:
        rows = [
            row
            for uuid in uuids
            if (row := self._proxy.proxy_row_for_uuid(uuid)) is not None
        ]
        root = self._qml.rootObject()
        if not rows or root is None:
            return
        list_view = root.findChild(QObject, "listView")
        if list_view is not None:
            QMetaObject.invokeMethod(
                list_view,
                "positionViewAtIndex",
                Qt.ConnectionType.DirectConnection,
                Q_ARG("int", rows[-1]),
                Q_ARG("int", _LIST_VIEW_CONTAIN),
            )
        self._set_highlighted(uuids)
        self._highlight_generation += 1
        asyncio.create_task(self._clear_highlight(self._highlight_generation))

    def _set_highlighted(self, uuids: list[str]) -> None:
        self._highlighted_uuids = uuids
        self.highlighted_uuids_changed.emit()

    async def _clear_highlight(self, generation: int) -> None:
        await asyncio.sleep(1.2)
        if generation == self._highlight_generation:
            self._set_highlighted([])

    @Slot("QVariantList")
    def showContextMenu(self, uuids: list[str]) -> None:
        menu = QMenu(self)
        menu.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        menu.setToolTipsVisible(True)
        add_update_action(menu, self._ctx, uuids, lambda: self._update_selected(uuids))
        menu.popup(self.cursor().pos())

    @asyncSlot()
    async def _update_selected(self, uuids: list[str]) -> None:
        await update_mods(self._ctx, uuids)

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
        self._model.commitOrder(list(active_uuids))

    def _on_config_changed(self, cfg: AppConfig) -> None:
        if self._compact_mode != cfg.compact_mod_list:
            self._compact_mode = cfg.compact_mod_list
            self.compact_mode_changed.emit(self._compact_mode)

    @Slot(list)
    def commitOrder(self, uuids: list[str]) -> None:
        self._model.commitOrder(uuids)
        self._ctx.set_active(self._model.active_uuids())
        self.order_changed.emit()

    @Slot(int, int)
    def moveRow(self, source_row: int, target_row: int) -> None:
        self._proxy.move_row(source_row, target_row, clamp_to_active=True)

    @Slot()
    def dragEnded(self) -> None:
        self._ctx.set_active(self._model.active_uuids())
        self.order_changed.emit()

    @Slot(list)
    def selectionChanged(self, uuids: list[str]) -> None:
        self.selection_changed.emit([uuid for uuid in uuids if uuid])

    # ── Private slots ─────────────────────────────────────────

    def _on_search_changed(self, text: str) -> None:
        self._proxy.set_search_filter(text)

    def eventFilter(self, obj: QObject, event: QEvent) -> bool:
        if obj is self._qml:
            if event.type() == QEvent.Type.FocusIn:
                self._qml.rootContext().setContextProperty("modListHasFocus", True)
            elif event.type() == QEvent.Type.FocusOut:
                self._qml.rootContext().setContextProperty("modListHasFocus", False)
        return super().eventFilter(obj, event)
