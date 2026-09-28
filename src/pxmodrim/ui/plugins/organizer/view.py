from __future__ import annotations

import asyncio
from pathlib import Path
from typing import TYPE_CHECKING, cast

from PySide6.QtCore import (
    Property,
    QMetaObject,
    QModelIndex,
    Qt,
    QTimer,
    QUrl,
    Signal,
    Slot,
)
from PySide6.QtGui import QColor
from PySide6.QtQml import QQmlEngine
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import (
    QDialog,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMenu,
    QMessageBox,
    QPushButton,
    QVBoxLayout,
    QWidget,
)
from qasync import asyncSlot

from pxmodrim.core.context import CoreContext
from pxmodrim.core.organizer import (
    MAX_DEPTH,
    ROOT_ID,
    OrganizerError,
    OrganizerService,
    RuleField,
    RuleOp,
    RuleSpec,
)
from pxmodrim.core.organizer.resolve import preview_rule_matches
from pxmodrim.ui.components.button import AppButton
from pxmodrim.ui.components.dialogs import await_dialog
from pxmodrim.ui.components.mod_activation import apply_activation, toggle_mods
from pxmodrim.ui.mod_selection import ModSelectionPresenter
from pxmodrim.ui.panels.mod_info_panel import ModInfoPanel
from pxmodrim.ui.plugins.organizer.dialogs import FolderNameDialog, FolderPickerDialog
from pxmodrim.ui.plugins.organizer.filter_model import OrganizerFilterModel
from pxmodrim.ui.plugins.organizer.tree_model import ModTreeModel, TreeNode
from pxmodrim.ui.theme.palette import PALETTE
from pxmodrim.ui.views.base import BaseViewPanel

if TYPE_CHECKING:
    from pxmodrim.ui.context import AppContext

_QML_DIR = Path(__file__).parent


class OrganizerViewPanel(BaseViewPanel):
    view_id = "organizer"
    icon_name = "folder"
    label = "Organizer"

    readyChanged = Signal()
    editorChanged = Signal()
    tagCreated = Signal()
    tagUpdated = Signal(int)
    rulesSaved = Signal()
    rulePreviewReady = Signal(int, "QVariantMap")  # pyright: ignore[reportArgumentType]

    def __init__(
        self,
        ctx: CoreContext,
        qml_engine: QQmlEngine | None = None,
        parent: QWidget | None = None,
        app_ctx: AppContext | None = None,
    ) -> None:
        super().__init__(ctx, qml_engine, parent, app_ctx=app_ctx)
        self._service = cast(OrganizerService, ctx.plugins.get("organizer"))
        self.model = ModTreeModel(ctx.diagnostics_service.summary_for, self)
        self.filters = OrganizerFilterModel(self)
        self._filter_key = "all"
        self._search_text = ""
        self._dirty = True
        self._selection_anchor = QModelIndex()
        self._selected_uuid: str | None = None
        self._editor_error = ""

        content = QWidget(self)
        row = QHBoxLayout(content)
        row.setContentsMargins(0, 0, 0, 0)
        row.setSpacing(0)

        self._sidebar = QQuickWidget(qml_engine, content)  # pyright: ignore[reportCallIssue, reportArgumentType]
        self._sidebar.setObjectName("organizerSidebar")
        self._sidebar.setFixedWidth(240)
        self._sidebar.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        self._sidebar.setClearColor(QColor(PALETTE["ELEVATE_2"]))
        sidebar_ctx = self._sidebar.rootContext()
        sidebar_ctx.setContextProperty("organizerPanel", self)
        sidebar_ctx.setContextProperty("organizerFilters", self.filters)
        self._sidebar.setSource(
            QUrl.fromLocalFile(str(_QML_DIR / "OrganizerSidebar.qml"))
        )
        row.addWidget(self._sidebar)

        main = QWidget(content)
        main_layout = QVBoxLayout(main)
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(0)
        toolbar = QWidget(main)
        toolbar.setObjectName("searchBox")
        toolbar.setFixedHeight(52)
        toolbar_layout = QHBoxLayout(toolbar)
        toolbar_layout.setContentsMargins(12, 8, 12, 8)
        self.search_input = QLineEdit(toolbar)
        self.search_input.setPlaceholderText("Search mods, package ID, author…")
        self.search_input.setClearButtonEnabled(True)
        toolbar_layout.addWidget(self.search_input, 1)
        create = QPushButton("New folder", toolbar)
        create.setObjectName("primaryAction")
        create.setCursor(Qt.CursorShape.PointingHandCursor)
        create.clicked.connect(self.newFolder)
        toolbar_layout.addWidget(create)
        self.rules_button = AppButton("Auto-rules", toolbar)
        self.rules_button.clicked.connect(self.openRules)
        toolbar_layout.addWidget(self.rules_button)
        for label, method in (
            ("Expand all", "expandAll"),
            ("Collapse all", "collapseAll"),
        ):
            button = AppButton(label, toolbar)
            button.clicked.connect(
                lambda _checked=False, name=method: self._tree_command(name)
            )
            toolbar_layout.addWidget(button)
        main_layout.addWidget(toolbar)

        self._qml = QQuickWidget(qml_engine, main)  # pyright: ignore[reportCallIssue, reportArgumentType]
        self._qml.setObjectName("organizerTree")
        self._qml.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        self._qml.setClearColor(QColor(PALETTE["ELEVATE_1"]))
        qml_ctx = self._qml.rootContext()
        qml_ctx.setContextProperty("organizerPanel", self)
        qml_ctx.setContextProperty("modTreeModel", self.model)
        self._qml.setSource(QUrl.fromLocalFile(str(_QML_DIR / "ModTree.qml")))
        hint = QWidget(main)
        hint.setObjectName("organizerHint")
        hint_layout = QHBoxLayout(hint)
        hint_layout.setContentsMargins(12, 5, 12, 5)
        hint_label = QLabel(
            "Folders and tags are for browsing only. "
            "Edit load order in Mods; checkboxes enable or disable the same mods.",
            hint,
        )
        hint_label.setWordWrap(True)
        hint_layout.addWidget(hint_label, 1)
        dismiss = AppButton("✕", hint)
        dismiss.clicked.connect(hint.hide)
        hint_layout.addWidget(dismiss)
        main_layout.addWidget(hint)
        main_layout.addWidget(self._qml, 1)
        self._selection_bar = QWidget(main)
        bar_layout = QHBoxLayout(self._selection_bar)
        bar_layout.setContentsMargins(12, 6, 12, 6)
        self._selection_label = QLabel(self._selection_bar)
        bar_layout.addWidget(self._selection_label)
        for label, action in (
            ("Enable", "enable"),
            ("Disable", "disable"),
            ("Move to…", "move"),
            ("Tags…", "tags"),
            ("Create folder from selection", "create"),
        ):
            button = AppButton(label, self._selection_bar)
            button.clicked.connect(
                lambda _checked=False, name=action: self.selectionAction(name)
            )
            bar_layout.addWidget(button)
        self._selection_bar.hide()
        main_layout.addWidget(self._selection_bar)
        row.addWidget(main, 3)
        self.mod_info = ModInfoPanel(
            self._ctx, self._qml_engine, ui_prefs=self._ui_prefs
        )
        self.mod_info.setObjectName("organizerModInfoPanel")
        self.mod_info.setMinimumWidth(300)
        row.addWidget(self.mod_info, 2)
        self._selection = ModSelectionPresenter(self._ctx, self.mod_info)
        self._root.addWidget(content, 1)
        self._status = QLabel(self)
        self._status.setObjectName("organizerStatus")
        self._status.setStyleSheet(f"color: {PALETTE['TEXT_DIM']}; padding: 5px 12px;")
        self._root.addWidget(self._status)

        self._menu = QMenu(self)
        self._menu.setToolTipsVisible(True)
        self._search_timer = QTimer(self)
        self.model.modelReset.connect(self._clear_selection_anchor)
        self._search_timer.setSingleShot(True)
        self._search_timer.setInterval(220)
        self.search_input.textChanged.connect(self._search_timer.start)
        self._search_timer.timeout.connect(self._search)
        self._service.changed.connect(self._invalidate)
        ctx.active_state_changed.connect(self._invalidate)
        ctx.mod_service.mods_changed.connect(self._invalidate)
        ctx.diagnostics_service.diagnostics_summary_changed.connect(
            self._refresh_diagnostics
        )
        self._service.changed.connect(self._notify_editor)

    @Property(bool, notify=readyChanged)
    def ready(self) -> bool:
        return self._service.ready

    def _notify_editor(self, _event: object) -> None:
        self.editorChanged.emit()

    def showEvent(self, event: object) -> None:
        super().showEvent(event)  # type: ignore[arg-type]
        if self._dirty:
            self._rebuild()
        root = self._qml.rootObject()
        if root is not None:
            QMetaObject.invokeMethod(root, "refreshRules")

    def _clear_selection_anchor(self) -> None:
        self._selection_anchor = QModelIndex()

    def _sync_info(self, force: bool = False) -> None:
        selected = self.model.selected_nodes()
        self.editorChanged.emit()
        mod_count = sum(node.kind == "mod" for node in selected)
        self._selection_label.setText(f"{mod_count} selected")
        self._selection_bar.setVisible(mod_count > 1)
        node = selected[0] if len(selected) == 1 else None
        uuid = node.leaf.uuid if node is not None and node.leaf is not None else None
        if uuid == self._selected_uuid and not force:
            return
        self._selected_uuid = uuid
        if uuid is None:
            self._selection.clear()
        else:
            asyncio.create_task(self._selection.show(uuid))

    def _invalidate(self, _event: object = None) -> None:
        self._dirty = True
        if self.isVisible():
            self._rebuild()

    def _refresh_diagnostics(self, _event: object) -> None:
        self.model.refresh_diagnostics()
        if self._selected_uuid is not None:
            self.mod_info.set_issues(
                self._ctx.diagnostics_service.issues_for(self._selected_uuid)
            )

    def _rebuild(self) -> None:
        if not self._service.ready:
            return
        self._dirty = False
        self.filters.update(self._service.tree_filters())
        query = self.filters.for_key(self._filter_key).query(self._search_text)
        tree = self._service.tree(query)
        self.model.set_tree(
            tree,
            self._ctx.all_mods,
            self._ctx.mod_service.provider_colors,
            not query.is_empty,
            self._service.state.tags,
        )
        full_tree = tree if query.is_empty else self._service.tree()
        self._status.setText(
            f"{len(self._ctx.all_mods)} mods    "
            f"{len(self._ctx.active_uuids)} active    "
            f"{len(self._service.state.folders) - 1} folders    "
            f"{len(full_tree.mods)} ungrouped"
        )
        self._sync_info(force=True)
        self.readyChanged.emit()

    def _search(self) -> None:
        text = self.search_input.text()
        if text != self._search_text:
            self._search_text = text
            self._invalidate()

    @Slot(str)
    def chooseFilter(self, key: str) -> None:
        if key != self._filter_key:
            self._filter_key = key
            self._invalidate()

    @asyncSlot(str)
    async def selectionAction(self, action: str) -> None:
        nodes = [node for node in self.model.selected_nodes() if node.leaf is not None]
        if not nodes:
            return
        if action == "tags":
            self.openTags()
        elif action in ("move", "create"):
            pids = self._selected_package_ids()
            if pids:
                if action == "move":
                    await self._move_mods(pids)
                else:
                    await self._new_folder_from(pids)
        elif action in ("enable", "disable"):
            uuids = [node.leaf.uuid for node in nodes if node.leaf is not None]
            if action == "enable":
                await self._enable_many(uuids)
            else:
                await self._disable_many(uuids)

    def _selected_package_ids(self) -> list[str]:
        return list(
            dict.fromkeys(
                node.leaf.package_id
                for node in self.model.selected_nodes()
                if node.kind == "mod" and node.leaf and node.leaf.package_id
            )
        )

    @Property(int, notify=editorChanged)
    def selectedTagCount(self) -> int:
        return len(self._selected_package_ids())

    @Property(list, notify=editorChanged)
    def tagRows(self) -> list[dict[str, str | int]]:
        if not self._service.ready:
            return []
        pids = self._selected_package_ids()
        rows: list[dict[str, str | int]] = []
        tags = sorted(
            self._service.state.tags.values(), key=lambda t: t.name.casefold()
        )
        for tag in tags:
            matched = sum(
                tag.id in self._service.state.mod_tags.get(pid, frozenset())
                for pid in pids
            )
            rows.append(
                {
                    "id": tag.id,
                    "name": tag.name,
                    "color": tag.color,
                    "checkState": 2 if 0 < matched < len(pids) else int(bool(matched)),
                }
            )
        return rows

    @Property(list, notify=editorChanged)
    def ruleRows(self) -> list[dict[str, str | int]]:
        if not self._service.ready:
            return []
        return [
            {
                "field": rule.field,
                "op": rule.op,
                "pattern": rule.pattern,
                "folder_id": rule.folder_id,
            }
            for rule in self._service.state.rules
        ]

    @Property(list, notify=editorChanged)
    def ruleFolders(self) -> list[dict[str, str | int]]:
        if not self._service.ready:
            return []
        folders = self._service.state.folders

        def path(folder_id: int) -> str:
            folder = folders[folder_id]
            if folder.parent_id == ROOT_ID:
                return folder.name
            assert folder.parent_id is not None
            return f"{path(folder.parent_id)} / {folder.name}"

        return [
            {"id": folder.id, "name": path(folder.id)}
            for folder in sorted(
                (f for f in folders.values() if f.id != ROOT_ID),
                key=lambda f: path(f.id).casefold(),
            )
        ]

    @Slot("QVariantList", int)
    def requestRulePreview(
        self, rows: list[dict[str, str | int]], revision: int
    ) -> None:
        try:
            loop = asyncio.get_running_loop()
        except RuntimeError:
            return
        loop.create_task(self._calculate_rule_preview(rows, revision))

    async def _calculate_rule_preview(
        self, rows: list[dict[str, str | int]], revision: int
    ) -> None:
        specs = [
            RuleSpec(
                cast(RuleField, row["field"]),
                cast(RuleOp, row["op"]),
                str(row["pattern"]),
                int(row["folder_id"]),
            )
            for row in rows
        ]
        counts, assignable = await asyncio.to_thread(
            preview_rule_matches, self._service.state, self._ctx.all_mods, specs
        )
        self.rulePreviewReady.emit(
            revision, {"counts": counts, "assignable": assignable}
        )

    @Property(str, notify=editorChanged)
    def editorError(self) -> str:
        return self._editor_error

    def _set_editor_error(self, message: str = "") -> None:
        if message != self._editor_error:
            self._editor_error = message
            self.editorChanged.emit()

    @Slot()
    def openTags(self) -> None:
        self._set_editor_error()
        root = self._qml.rootObject()
        if root is not None:
            QMetaObject.invokeMethod(root, "openTags")

    @Slot()
    def openRules(self) -> None:
        self._set_editor_error()
        root = self._qml.rootObject()
        if root is not None:
            QMetaObject.invokeMethod(root, "openRules")

    @asyncSlot(str, str)
    async def createTag(self, name: str, color: str) -> None:
        try:
            await self._service.create_tag(name, color)
            self.tagCreated.emit()
            self._set_editor_error()
        except OrganizerError as exc:
            self._set_editor_error(str(exc))

    @asyncSlot(int, str, str)
    async def updateTag(self, tag_id: int, name: str, color: str) -> None:
        try:
            await self._service.update_tag(tag_id, name, color)
            self.tagUpdated.emit(tag_id)
            self._set_editor_error()
        except OrganizerError as exc:
            self._set_editor_error(str(exc))

    @asyncSlot(int)
    async def deleteTag(self, tag_id: int) -> None:
        try:
            await self._service.delete_tag(tag_id)
            self._set_editor_error()
        except OrganizerError as exc:
            self._set_editor_error(str(exc))

    @asyncSlot(int, bool)
    async def assignTag(self, tag_id: int, add: bool) -> None:
        pids = self._selected_package_ids()
        try:
            if add:
                await self._service.set_mod_tags(pids, add=(tag_id,))
            else:
                await self._service.set_mod_tags(pids, remove=(tag_id,))
            self._set_editor_error()
        except OrganizerError as exc:
            self._set_editor_error(str(exc))

    @asyncSlot("QVariantList")
    async def saveRules(self, rows: list[dict[str, str | int]]) -> None:
        specs = [
            RuleSpec(
                cast(RuleField, row["field"]),
                cast(RuleOp, row["op"]),
                str(row["pattern"]),
                int(row["folder_id"]),
            )
            for row in rows
        ]
        try:
            await self._service.set_rules(specs)
            self._set_editor_error()
            self.rulesSaved.emit()
        except OrganizerError as exc:
            self._set_editor_error(str(exc))

    @asyncSlot()
    async def addStandardRules(self) -> None:
        try:
            await self._service.add_standard_rules()
            self._set_editor_error()
        except OrganizerError as exc:
            self._set_editor_error(str(exc))

    @Slot(result="QVariantList")
    def expandedIndexes(self) -> list[QModelIndex]:
        return self.model.expanded_indexes()

    @Slot(QModelIndex, int, "QVariantList")
    def selectRow(
        self, index: QModelIndex, modifiers: int, range_indexes: list[QModelIndex]
    ) -> None:
        if (
            modifiers & Qt.KeyboardModifier.ShiftModifier.value
            and self._selection_anchor.isValid()
        ):
            self.model.select(range_indexes, "replace")
        elif modifiers & Qt.KeyboardModifier.ControlModifier.value:
            self.model.select((index,), "toggle")
            self._selection_anchor = index
        else:
            self.model.select((index,))
            self._selection_anchor = index
        self._sync_info()

    @Slot(QModelIndex)
    def selectSingle(self, index: QModelIndex) -> None:
        if not self.model.is_selected(index):
            self.model.select((index,))
        self._selection_anchor = index
        self._sync_info()

    def _tree_command(self, method: str) -> None:
        root = self._qml.rootObject()
        if root is not None:
            QMetaObject.invokeMethod(root, method)

    @asyncSlot(bool)
    async def persistAllCollapsed(self, collapsed: bool) -> None:
        await self._service.set_all_collapsed(collapsed)

    @asyncSlot(QModelIndex, bool)
    async def persistCollapsed(self, index: QModelIndex, collapsed: bool) -> None:
        node = self.model.node_at(index)
        if node is not None and node.is_container and not self.model.filtering:
            try:
                await self._service.set_collapsed(node.folder_id, collapsed)
            except OrganizerError as exc:
                await self._error(exc)

    @asyncSlot(QModelIndex)
    async def toggleCheck(self, index: QModelIndex) -> None:
        node = self.model.node_at(index)
        if node is None:
            return
        if node.kind == "mod" and node.leaf is not None:
            await toggle_mods(self._ctx, self, (node.leaf.uuid,))
        elif node.is_container:
            await self._toggle_folder(node)

    async def _toggle_folder(self, node: TreeNode) -> None:
        enable, disable = self._service.folder_toggle(
            node.folder_id, recursive=node.kind != "ungrouped"
        )
        if enable or disable:
            await apply_activation(self._ctx, self, enable=enable, disable=disable)

    @Slot(QModelIndex)
    def contextMenu(self, index: QModelIndex) -> None:
        node = self.model.node_at(index)
        if node is None:
            self._menu.clear()
            self._menu.addAction("New folder...", self.newFolder)
        elif node.kind == "mod":
            self.selectSingle(index)
            self._mod_menu()
        else:
            self.model.select((index,))
            self._folder_menu(node)
        self._sync_info()
        self._menu.popup(self.cursor().pos())

    def _mod_menu(self) -> None:
        nodes = [n for n in self.model.selected_nodes() if n.kind == "mod" and n.leaf]
        uuids = [n.leaf.uuid for n in nodes if n.leaf]
        active = set(self._ctx.active_uuids)
        self._menu.clear()
        if any(uuid not in active for uuid in uuids):
            self._menu.addAction("Enable", lambda: self._enable_many(uuids))
        if any(uuid in active for uuid in uuids):
            self._menu.addAction("Disable", lambda: self._disable_many(uuids))
        self._menu.addSeparator()
        pids = list(
            dict.fromkeys(
                n.leaf.package_id for n in nodes if n.leaf and n.leaf.package_id
            )
        )
        placeable = bool(pids) and all(n.leaf and n.leaf.package_id for n in nodes)
        tooltip = "Mods without a package ID cannot be placed in folders"
        move = self._menu.addAction("Move to folder...", lambda: self._move_mods(pids))
        remove = self._menu.addAction("Remove from folder", lambda: self._ungroup(pids))
        create = self._menu.addAction(
            "Create folder from selection...", lambda: self._new_folder_from(pids)
        )
        for action in (move, remove, create):
            action.setEnabled(placeable)
            action.setToolTip(tooltip if not placeable else "")
        remove.setEnabled(placeable and any(n.folder_id != ROOT_ID for n in nodes))
        tag_action = self._menu.addAction("Manage tags...", self.openTags)
        tag_action.setEnabled(placeable)
        manual = list(
            dict.fromkeys(
                n.leaf.package_id
                for n in nodes
                if n.leaf and n.leaf.package_id and n.leaf.placement == "manual"
            )
        )
        if placeable and manual:
            self._menu.addAction("Use auto rules", lambda: self._reset_rules(manual))

    def _folder_menu(self, node: TreeNode) -> None:
        self._menu.clear()
        recursive = node.kind != "ungrouped"
        uuids = self._service.folder_mod_uuids(node.folder_id, recursive)
        self._menu.addAction("Enable all", lambda: self._enable_many(uuids)).setEnabled(
            bool(uuids)
        )
        self._menu.addAction(
            "Disable all", lambda: self._disable_many(uuids)
        ).setEnabled(bool(uuids))
        self._menu.addSeparator()
        if node.kind == "ungrouped":
            self._menu.addAction("New folder...", self.newFolder)
            return
        folder_id = node.folder_id
        if node.folder is not None and node.folder.depth < MAX_DEPTH:
            self._menu.addAction(
                "New subfolder...", lambda: self._new_folder(folder_id)
            )
        self._menu.addAction("Rename...", lambda: self._rename(folder_id))
        move = self._menu.addAction("Move to...", lambda: self._move_folder(folder_id))
        move.setEnabled(bool(self._service.folder_move_targets(folder_id)))
        self._menu.addAction("Delete...", lambda: self._delete(folder_id))

    @asyncSlot()
    async def newFolder(self) -> None:
        await self._new_folder(ROOT_ID)

    @asyncSlot()
    async def _new_folder(self, parent_id: int) -> None:
        result, dialog = await await_dialog(FolderNameDialog, "New folder", "", self)
        if result == QDialog.DialogCode.Accepted:
            try:
                await self._service.create_folder(dialog.textValue(), parent_id)
            except OrganizerError as exc:
                await self._error(exc)

    @asyncSlot()
    async def _new_folder_from(self, package_ids: list[str]) -> None:
        result, dialog = await await_dialog(
            FolderNameDialog, "Create folder from selection", "", self
        )
        if result == QDialog.DialogCode.Accepted:
            try:
                await self._service.create_folder_from(dialog.textValue(), package_ids)
            except OrganizerError as exc:
                await self._error(exc)

    @asyncSlot()
    async def _rename(self, folder_id: int) -> None:
        current = self._service.state.folders[folder_id]
        result, dialog = await await_dialog(
            FolderNameDialog, "Rename folder", current.name, self
        )
        if result == QDialog.DialogCode.Accepted:
            try:
                await self._service.rename_folder(folder_id, dialog.textValue())
            except OrganizerError as exc:
                await self._error(exc)

    @asyncSlot()
    async def _move_mods(self, package_ids: list[str]) -> None:
        allowed = frozenset(self._service.state.folders)
        result, dialog = await await_dialog(
            FolderPickerDialog,
            self._service.tree(),
            "Move mods to folder",
            allowed,
            self,
        )
        if result == QDialog.DialogCode.Accepted:
            try:
                if dialog.folder_id == ROOT_ID:
                    await self._service.ungroup(package_ids)
                else:
                    await self._service.place(package_ids, dialog.folder_id)
            except OrganizerError as exc:
                await self._error(exc)

    @asyncSlot()
    async def _move_folder(self, folder_id: int) -> None:
        targets = self._service.folder_move_targets(folder_id)
        result, dialog = await await_dialog(
            FolderPickerDialog, self._service.tree(), "Move folder to...", targets, self
        )
        if result == QDialog.DialogCode.Accepted:
            try:
                await self._service.move_folder(folder_id, dialog.folder_id)
            except OrganizerError as exc:
                await self._error(exc)

    @asyncSlot()
    async def _delete(self, folder_id: int) -> None:
        folder = self._service.state.folders[folder_id]
        result, _ = await await_dialog(
            QMessageBox,
            QMessageBox.Icon.Warning,
            "Delete folder?",
            (
                f'Delete "{folder.name}" and its subfolders? '
                "Their mods become ungrouped. Load order is not affected."
            ),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            self,
        )
        if result == QMessageBox.StandardButton.Yes:
            try:
                await self._service.delete_folder(folder_id)
            except OrganizerError as exc:
                await self._error(exc)

    @asyncSlot()
    async def _ungroup(self, package_ids: list[str]) -> None:
        try:
            await self._service.ungroup(package_ids)
        except OrganizerError as exc:
            await self._error(exc)

    @asyncSlot()
    async def _reset_rules(self, package_ids: list[str]) -> None:
        try:
            await self._service.reset_to_rules(package_ids)
        except OrganizerError as exc:
            await self._error(exc)

    @asyncSlot()
    async def _enable_many(self, uuids: list[str]) -> None:
        await apply_activation(self._ctx, self, enable=uuids)

    @asyncSlot()
    async def _disable_many(self, uuids: list[str]) -> None:
        await apply_activation(self._ctx, self, disable=uuids)

    @Slot(QModelIndex, QModelIndex, result=bool)
    def canDrop(self, source: QModelIndex, target: QModelIndex) -> bool:
        src = self.model.node_at(source)
        dst = self.model.node_at(target)
        if src is None or dst is None or not dst.is_container or src is dst:
            return False
        if src.kind == "mod":
            selected = (
                self.model.selected_nodes() if self.model.is_selected(source) else [src]
            )
            nodes = [n for n in selected if n.kind == "mod"]
            return bool(nodes) and all(n.leaf and n.leaf.package_id for n in nodes)
        if src.kind == "folder":
            return dst.folder_id in self._service.folder_move_targets(src.folder_id)
        return False

    @asyncSlot(QModelIndex, QModelIndex)
    async def dropOn(self, source: QModelIndex, target: QModelIndex) -> None:
        if not self.canDrop(source, target):
            return
        src = self.model.node_at(source)
        dst = self.model.node_at(target)
        if src is None or dst is None:
            return
        try:
            if src.kind == "folder":
                await self._service.move_folder(src.folder_id, dst.folder_id)
            else:
                selected = (
                    self.model.selected_nodes()
                    if self.model.is_selected(source)
                    else [src]
                )
                pids = list(
                    dict.fromkeys(
                        n.leaf.package_id
                        for n in selected
                        if n.kind == "mod" and n.leaf and n.leaf.package_id
                    )
                )
                if dst.folder_id == ROOT_ID:
                    await self._service.ungroup(pids)
                else:
                    await self._service.place(pids, dst.folder_id)
        except OrganizerError as exc:
            await self._error(exc)

    async def _error(self, exc: OrganizerError) -> None:
        await await_dialog(
            QMessageBox,
            QMessageBox.Icon.Warning,
            "Organizer",
            str(exc),
            QMessageBox.StandardButton.Ok,
            self,
        )
