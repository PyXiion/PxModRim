from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator, Mapping
from typing import TYPE_CHECKING, Any, Literal

from PySide6.QtCore import (
    QAbstractItemModel,
    QByteArray,
    QModelIndex,
    QObject,
    QPersistentModelIndex,
    Qt,
)

from pxmodrim.core.organizer import ROOT_ID, FolderNode, ModLeaf
from pxmodrim.ui.theme.palette import PALETTE

if TYPE_CHECKING:
    from pxmodrim.core.models.metadata.structures import ListedMod
    from pxmodrim.core.models.view.diagnostics import ModDiagnosticsView

NodeKind = Literal["root", "folder", "ungrouped", "mod"]
AnyIndex = QModelIndex | QPersistentModelIndex

UNGROUPED_KEY = "ungrouped"

PROVIDER_ICONS: dict[str, str] = {
    "steam": "steam",
    "downloaded": "steam",
    "local": "folder",
    "core": "grid",
    "git": "git",
}

_CHECK_STATES = {
    "on": Qt.CheckState.Checked,
    "off": Qt.CheckState.Unchecked,
    "partial": Qt.CheckState.PartiallyChecked,
}


class TreeNode:
    """One row of the organizer tree; the model's internal pointer target."""

    __slots__ = (
        "children",
        "folder",
        "key",
        "kind",
        "leaf",
        "parent",
        "provider_id",
        "row",
    )

    def __init__(
        self,
        kind: NodeKind,
        key: str,
        parent: TreeNode | None,
        folder: FolderNode | None = None,
        leaf: ModLeaf | None = None,
        provider_id: str = "",
    ) -> None:
        self.kind: NodeKind = kind
        self.key = key
        self.parent = parent
        self.folder = folder
        self.leaf = leaf
        self.provider_id = provider_id
        self.children: list[TreeNode] = []
        self.row = 0

    def add(self, child: TreeNode) -> None:
        child.row = len(self.children)
        self.children.append(child)

    def walk(self) -> Iterator[TreeNode]:
        for child in self.children:
            yield child
            yield from child.walk()

    @property
    def folder_id(self) -> int:
        """Folder this node is (folder/ungrouped) or sits in (mod)."""
        if self.kind == "folder" and self.folder is not None:
            return self.folder.folder.id
        if self.kind == "mod" and self.parent is not None:
            return self.parent.folder_id
        return ROOT_ID

    @property
    def is_container(self) -> bool:
        return self.kind in ("folder", "ungrouped")


def _build(
    root: FolderNode, mods: Mapping[str, ListedMod], filtering: bool
) -> TreeNode:
    top = TreeNode("root", "", None)

    def add_mods(parent: TreeNode, leaves: Iterable[ModLeaf]) -> None:
        for leaf in leaves:
            mod = mods.get(leaf.uuid)
            provider = mod.provider_id if mod is not None else ""
            parent.add(
                TreeNode(
                    "mod", f"m:{leaf.uuid}", parent, leaf=leaf, provider_id=provider
                )
            )

    def add_folder(parent: TreeNode, node: FolderNode) -> None:
        item = TreeNode("folder", f"f:{node.folder.id}", parent, folder=node)
        parent.add(item)
        for child in node.children:
            add_folder(item, child)
        add_mods(item, node.mods)

    for child in root.children:
        add_folder(top, child)
    if root.mods or not filtering:
        ungrouped = TreeNode("ungrouped", UNGROUPED_KEY, top, folder=root)
        top.add(ungrouped)
        add_mods(ungrouped, root.mods)
    return top


def _same_shape(a: TreeNode, b: TreeNode) -> bool:
    if a.key != b.key or len(a.children) != len(b.children):
        return False
    return all(_same_shape(x, y) for x, y in zip(a.children, b.children, strict=True))


def _copy_payload(dst: TreeNode, src: TreeNode) -> None:
    dst.folder = src.folder
    dst.leaf = src.leaf
    dst.provider_id = src.provider_id
    for d, s in zip(dst.children, src.children, strict=True):
        _copy_payload(d, s)


class ModTreeModel(QAbstractItemModel):
    """Organizer folders (and the trailing Ungrouped node) with their mods."""

    KindRole = Qt.ItemDataRole.UserRole + 1
    KeyRole = Qt.ItemDataRole.UserRole + 2
    UuidRole = Qt.ItemDataRole.UserRole + 3
    PackageIdRole = Qt.ItemDataRole.UserRole + 4
    CheckStateRole = Qt.ItemDataRole.UserRole + 5
    EnabledCountRole = Qt.ItemDataRole.UserRole + 6
    TotalCountRole = Qt.ItemDataRole.UserRole + 7
    VisibleCountRole = Qt.ItemDataRole.UserRole + 8
    FilteringRole = Qt.ItemDataRole.UserRole + 9
    HasRuleRole = Qt.ItemDataRole.UserRole + 10
    ProviderIconRole = Qt.ItemDataRole.UserRole + 11
    ProviderColorRole = Qt.ItemDataRole.UserRole + 12
    HasErrorRole = Qt.ItemDataRole.UserRole + 13
    ErrorTooltipRole = Qt.ItemDataRole.UserRole + 14
    CanPlaceRole = Qt.ItemDataRole.UserRole + 15
    SelectedRole = Qt.ItemDataRole.UserRole + 16
    HasWarningRole = Qt.ItemDataRole.UserRole + 17
    WarningTooltipRole = Qt.ItemDataRole.UserRole + 18

    _SELECTION_ROLES = (SelectedRole,)
    _ERROR_ROLES = (HasErrorRole, ErrorTooltipRole, HasWarningRole, WarningTooltipRole)

    def __init__(
        self,
        summary_for: Callable[[str], ModDiagnosticsView | None],
        parent: QObject | None = None,
    ) -> None:
        super().__init__(parent)
        self._summary_for = summary_for
        self._root = TreeNode("root", "", None)
        self._filtering = False
        self._provider_colors: Mapping[str, str] = {}
        self._selected: set[str] = set()

    def _node(self, index: AnyIndex | None) -> TreeNode:
        if index is None or not index.isValid():
            return self._root
        return index.internalPointer()

    def index(
        self, row: int, column: int, parent: AnyIndex | None = None
    ) -> QModelIndex:
        node = self._node(parent)
        if column != 0 or not 0 <= row < len(node.children):
            return QModelIndex()
        return self.createIndex(row, 0, node.children[row])

    def parent(self, child: AnyIndex | None = None) -> QModelIndex:  # type: ignore[override]  # pyright: ignore[reportIncompatibleMethodOverride]
        if child is None or not child.isValid():
            return QModelIndex()
        up = self._node(child).parent
        if up is None or up is self._root:
            return QModelIndex()
        return self.createIndex(up.row, 0, up)

    def rowCount(self, parent: AnyIndex | None = None) -> int:
        if parent is not None and parent.column() > 0:
            return 0
        return len(self._node(parent).children)

    def columnCount(self, parent: AnyIndex | None = None) -> int:
        return 1

    def data(self, index: AnyIndex, role: int = Qt.ItemDataRole.DisplayRole) -> Any:
        if not index.isValid():
            return None
        node: TreeNode = index.internalPointer()
        if role == Qt.ItemDataRole.DisplayRole:
            return self._name(node)
        if role == self.KindRole:
            return node.kind
        if role == self.KeyRole:
            return node.key
        if role == self.SelectedRole:
            return node.key in self._selected
        if role == self.FilteringRole:
            return self._filtering
        if node.kind == "mod":
            return self._mod_data(node, role)
        return self._folder_data(node, role)

    def _name(self, node: TreeNode) -> str:
        if node.kind == "ungrouped":
            return "Ungrouped"
        if node.kind == "folder" and node.folder is not None:
            return node.folder.folder.name
        return node.leaf.name if node.leaf is not None else ""

    def _folder_data(self, node: TreeNode, role: int) -> Any:
        folder = node.folder
        if folder is None:
            return None
        own = node.kind == "ungrouped"
        if role == self.CheckStateRole:
            return _CHECK_STATES[folder.own_check if own else folder.check]
        if role == self.EnabledCountRole:
            return folder.own_enabled if own else folder.enabled
        if role == self.TotalCountRole:
            return folder.own_total if own else folder.total
        if role == self.VisibleCountRole:
            return len(folder.mods) if own else folder.visible_total
        if role == self.HasRuleRole:
            return folder.has_rules and not own
        return None

    def _mod_data(self, node: TreeNode, role: int) -> Any:
        leaf = node.leaf
        if leaf is None:
            return None
        if role == self.UuidRole:
            return leaf.uuid
        if role == self.PackageIdRole:
            return leaf.package_id or ""
        if role == self.CheckStateRole:
            return Qt.CheckState.Checked if leaf.enabled else Qt.CheckState.Unchecked
        if role == self.ProviderIconRole:
            return PROVIDER_ICONS.get(node.provider_id, "folder")
        if role == self.ProviderColorRole:
            return self._provider_colors.get(node.provider_id, PALETTE["TEXT_DIM"])
        if role == self.CanPlaceRole:
            return leaf.package_id is not None
        if role in self._ERROR_ROLES:
            summary = self._summary_for(leaf.uuid) if leaf.enabled else None
            if role == self.HasErrorRole:
                return summary is not None and summary.has_errors
            if role == self.HasWarningRole:
                return summary is not None and summary.has_warnings
            if role == self.ErrorTooltipRole:
                return summary.error_tooltip if summary is not None else ""
            return summary.warning_tooltip if summary is not None else ""
        return None

    def roleNames(self) -> dict[int, QByteArray]:
        return {
            Qt.ItemDataRole.DisplayRole: QByteArray(b"name"),
            self.KindRole: QByteArray(b"kind"),
            self.KeyRole: QByteArray(b"key"),
            self.UuidRole: QByteArray(b"uuid"),
            self.PackageIdRole: QByteArray(b"packageId"),
            self.CheckStateRole: QByteArray(b"checkState"),
            self.EnabledCountRole: QByteArray(b"enabledCount"),
            self.TotalCountRole: QByteArray(b"totalCount"),
            self.VisibleCountRole: QByteArray(b"visibleCount"),
            self.FilteringRole: QByteArray(b"filtering"),
            self.HasRuleRole: QByteArray(b"hasRule"),
            self.ProviderIconRole: QByteArray(b"providerIcon"),
            self.ProviderColorRole: QByteArray(b"providerColor"),
            self.HasErrorRole: QByteArray(b"hasError"),
            self.ErrorTooltipRole: QByteArray(b"errorTooltip"),
            self.HasWarningRole: QByteArray(b"hasWarning"),
            self.WarningTooltipRole: QByteArray(b"warningTooltip"),
            self.CanPlaceRole: QByteArray(b"canPlace"),
            self.SelectedRole: QByteArray(b"selected"),
        }

    @property
    def filtering(self) -> bool:
        return self._filtering

    def set_tree(
        self,
        root: FolderNode,
        mods: Mapping[str, ListedMod],
        provider_colors: Mapping[str, str],
        filtering: bool,
    ) -> None:
        """Swap in a freshly built tree.

        Same-shape rebuilds (check toggles, counts, collapse flags) update in
        place so the view keeps its expansion and scroll position.
        """
        fresh = _build(root, mods, filtering)
        self._provider_colors = provider_colors
        if self._filtering == filtering and _same_shape(self._root, fresh):
            _copy_payload(self._root, fresh)
            self._emit_all_changed(self._root)
        else:
            self.beginResetModel()
            self._root = fresh
            self._filtering = filtering
            self.endResetModel()
        live = {node.key for node in self._root.walk()}
        self._selected &= live

    def _emit_all_changed(self, node: TreeNode, roles: Iterable[int] = ()) -> None:
        if not node.children:
            return
        parent = self._index_of(node)
        top = self.index(0, 0, parent)
        bottom = self.index(len(node.children) - 1, 0, parent)
        self.dataChanged.emit(top, bottom, list(roles))
        for child in node.children:
            self._emit_all_changed(child, roles)

    def refresh_diagnostics(self) -> None:
        self._emit_all_changed(self._root, self._ERROR_ROLES)

    def _index_of(self, node: TreeNode) -> QModelIndex:
        if node is self._root:
            return QModelIndex()
        return self.createIndex(node.row, 0, node)

    def node_at(self, index: AnyIndex) -> TreeNode | None:
        return index.internalPointer() if index.isValid() else None

    def index_for_key(self, key: str) -> QModelIndex:
        for node in self._root.walk():
            if node.key == key:
                return self._index_of(node)
        return QModelIndex()

    def expanded_indexes(self, parent: AnyIndex | None = None) -> list[QModelIndex]:
        """Containers under *parent* the view should expand, parents first.

        Everything opens while filtering so matches are never hidden; otherwise
        the persisted ``collapsed`` flag decides and closed folders hide their
        subtree.
        """
        result: list[QModelIndex] = []

        def visit(node: TreeNode) -> None:
            for child in node.children:
                if child.is_container and self._is_open(child):
                    result.append(self._index_of(child))
                    visit(child)

        visit(self._node(parent))
        return result

    def _is_open(self, node: TreeNode) -> bool:
        if self._filtering:
            return True
        return node.folder is not None and not node.folder.folder.collapsed

    def selected_nodes(self) -> list[TreeNode]:
        return [node for node in self._root.walk() if node.key in self._selected]

    def is_selected(self, index: AnyIndex) -> bool:
        node = self.node_at(index)
        return node is not None and node.key in self._selected

    def select(self, indexes: Iterable[AnyIndex], mode: str = "replace") -> None:
        """Apply a click to the selection: ``replace``, ``toggle`` or ``extend``."""
        keys = [n.key for n in map(self.node_at, indexes) if n is not None]
        before = set(self._selected)
        if mode == "replace":
            self._selected = set(keys)
        elif mode == "toggle":
            self._selected ^= set(keys)
        else:
            self._selected |= set(keys)
        self._notify_selection(before ^ self._selected)

    def clear_selection(self) -> None:
        before = set(self._selected)
        self._selected.clear()
        self._notify_selection(before)

    def _notify_selection(self, changed: set[str]) -> None:
        if not changed:
            return
        for node in self._root.walk():
            if node.key in changed:
                index = self._index_of(node)
                self.dataChanged.emit(index, index, list(self._SELECTION_ROLES))
