from __future__ import annotations

import re
from collections.abc import Iterable, Sequence
from typing import TYPE_CHECKING, get_args

import msgspec

from pxmodrim.core.events import Event
from pxmodrim.core.organizer.defaults import STANDARD_RULES, StandardRule
from pxmodrim.core.organizer.models import (
    MAX_DEPTH,
    ROOT_ID,
    Folder,
    OrganizerError,
    OrganizerState,
    RuleField,
    RuleOp,
    RuleSpec,
    Tag,
)
from pxmodrim.core.organizer.resolve import (
    FolderNode,
    TreeFilter,
    TreeQuery,
    build_tree,
    tree_filters,
)
from pxmodrim.core.plugin import Plugin

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext
    from pxmodrim.core.organizer.db import OrganizerDb

_COLOR_RE = re.compile(r"#(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})")
_RULE_FIELDS = frozenset(get_args(RuleField))
_RULE_OPS = frozenset(get_args(RuleOp))


def _clean_name(name: str, what: str) -> str:
    cleaned = name.strip()
    if not cleaned:
        raise OrganizerError(f"{what} name cannot be empty.")
    return cleaned


def _clean_package_ids(package_ids: Iterable[str]) -> list[str]:
    return list(dict.fromkeys(p.strip().lower() for p in package_ids if p.strip()))


class OrganizerService(Plugin):
    """Virtual folder tree, tags and auto-folder rules; never touches load order."""

    name = "organizer"

    changed: Event[None]

    __slots__ = ("_ctx", "_db", "_state", "changed")

    def __init__(self, ctx: CoreContext, db: OrganizerDb) -> None:
        self._ctx = ctx
        self._db = db
        self._state: OrganizerState | None = None
        self.changed = Event()

    async def init(self, ctx: CoreContext) -> None:
        self._state = await self._db.load()
        self.changed.emit(None)

    async def shutdown(self) -> None:
        await self._db.close()

    @property
    def ready(self) -> bool:
        """Whether ``init()`` has loaded state; views built earlier must wait."""
        return self._state is not None

    @property
    def state(self) -> OrganizerState:
        if self._state is None:
            raise RuntimeError("OrganizerService accessed before init()")
        return self._state

    async def _commit(self) -> None:
        self._state = await self._db.load()
        self.changed.emit(None)

    # ── Validation helpers ────────────────────────────────────────────────────

    def _folder(self, folder_id: int) -> Folder:
        folder = self.state.folders.get(folder_id)
        if folder is None:
            raise OrganizerError("That folder no longer exists.")
        return folder

    def _non_root(self, folder_id: int) -> Folder:
        if folder_id == ROOT_ID:
            raise OrganizerError("The root folder cannot be changed.")
        return self._folder(folder_id)

    def _tag(self, tag_id: int) -> Tag:
        tag = self.state.tags.get(tag_id)
        if tag is None:
            raise OrganizerError("That tag no longer exists.")
        return tag

    def _depth(self, folder_id: int) -> int:
        depth = 0
        current = self._folder(folder_id)
        while current.parent_id is not None:
            depth += 1
            current = self._folder(current.parent_id)
        return depth

    def _children(self, folder_id: int) -> list[Folder]:
        return [f for f in self.state.folders.values() if f.parent_id == folder_id]

    def _subtree_ids(self, folder_id: int) -> set[int]:
        ids = {folder_id}
        pending = [folder_id]
        while pending:
            for child in self._children(pending.pop()):
                if child.id not in ids:
                    ids.add(child.id)
                    pending.append(child.id)
        return ids

    def _height(self, folder_id: int) -> int:
        children = self._children(folder_id)
        return 1 + max((self._height(c.id) for c in children), default=0)

    def _check_depth(self, parent_id: int, height: int) -> None:
        if self._depth(parent_id) + height > MAX_DEPTH:
            raise OrganizerError(
                f"Folders can be nested at most {MAX_DEPTH} levels deep."
            )

    def _check_unique_folder_name(
        self, parent_id: int, name: str, exclude: int | None = None
    ) -> None:
        key = name.casefold()
        for sibling in self._children(parent_id):
            if sibling.id != exclude and sibling.name.casefold() == key:
                raise OrganizerError(f'A folder named "{name}" already exists here.')

    def _check_tag(self, name: str, color: str, exclude: int | None = None) -> str:
        cleaned = _clean_name(name, "Tag")
        key = cleaned.casefold()
        for tag in self.state.tags.values():
            if tag.id != exclude and tag.name.casefold() == key:
                raise OrganizerError(f'A tag named "{cleaned}" already exists.')
        if _COLOR_RE.fullmatch(color) is None:
            raise OrganizerError(f'"{color}" is not a valid color (use #RRGGBB).')
        return cleaned

    # ── Folders ───────────────────────────────────────────────────────────────

    async def create_folder(self, name: str, parent_id: int = ROOT_ID) -> Folder:
        folder = await self._create_folder(name, parent_id)
        await self._commit()
        return folder

    async def _create_folder(self, name: str, parent_id: int) -> Folder:
        cleaned = _clean_name(name, "Folder")
        self._folder(parent_id)
        self._check_depth(parent_id, 1)
        self._check_unique_folder_name(parent_id, cleaned)
        return await self._db.create_folder(parent_id, cleaned)

    async def create_folder_from(
        self, name: str, package_ids: Iterable[str], parent_id: int = ROOT_ID
    ) -> Folder:
        cleaned = _clean_name(name, "Folder")
        self._folder(parent_id)
        self._check_depth(parent_id, 1)
        self._check_unique_folder_name(parent_id, cleaned)
        pids = _clean_package_ids(package_ids)
        folder = await self._db.create_folder_with(parent_id, cleaned, pids)
        await self._commit()
        return folder

    async def rename_folder(self, folder_id: int, name: str) -> None:
        folder = self._non_root(folder_id)
        cleaned = _clean_name(name, "Folder")
        if cleaned == folder.name:
            return
        parent_id = folder.parent_id if folder.parent_id is not None else ROOT_ID
        self._check_unique_folder_name(parent_id, cleaned, exclude=folder_id)
        await self._db.rename_folder(folder_id, cleaned)
        await self._commit()

    async def move_folder(self, folder_id: int, parent_id: int) -> None:
        folder = self._non_root(folder_id)
        self._folder(parent_id)
        if parent_id == folder.parent_id:
            return
        if parent_id in self._subtree_ids(folder_id):
            raise OrganizerError("A folder cannot be moved into itself.")
        self._check_depth(parent_id, self._height(folder_id))
        self._check_unique_folder_name(parent_id, folder.name, exclude=folder_id)
        await self._db.set_parent(folder_id, parent_id)
        await self._commit()

    async def set_collapsed(self, folder_id: int, collapsed: bool) -> None:
        if self._folder(folder_id).collapsed == collapsed:
            return
        await self._db.set_collapsed(folder_id, collapsed)
        await self._commit()

    async def set_all_collapsed(self, collapsed: bool) -> None:
        if all(folder.collapsed == collapsed for folder in self.state.folders.values()):
            return
        await self._db.set_all_collapsed(collapsed)
        await self._commit()

    async def delete_folder(self, folder_id: int) -> None:
        self._non_root(folder_id)
        await self._db.delete_folder(folder_id)
        await self._commit()

    # ── Placement ─────────────────────────────────────────────────────────────

    async def place(self, package_ids: Iterable[str], folder_id: int) -> None:
        self._folder(folder_id)
        pids = _clean_package_ids(package_ids)
        if not pids:
            return
        await self._db.place(pids, folder_id)
        await self._commit()

    async def ungroup(self, package_ids: Iterable[str]) -> None:
        await self.place(package_ids, ROOT_ID)

    async def reset_to_rules(self, package_ids: Iterable[str]) -> None:
        pids = _clean_package_ids(package_ids)
        if not pids:
            return
        await self._db.unplace(pids)
        await self._commit()

    # ── Tags ──────────────────────────────────────────────────────────────────

    async def create_tag(self, name: str, color: str) -> Tag:
        cleaned = self._check_tag(name, color)
        tag = await self._db.create_tag(cleaned, color)
        await self._commit()
        return tag

    async def update_tag(self, tag_id: int, name: str, color: str) -> None:
        self._tag(tag_id)
        cleaned = self._check_tag(name, color, exclude=tag_id)
        await self._db.update_tag(tag_id, cleaned, color)
        await self._commit()

    async def delete_tag(self, tag_id: int) -> None:
        self._tag(tag_id)
        await self._db.delete_tag(tag_id)
        await self._commit()

    async def set_mod_tags(
        self,
        package_ids: Iterable[str],
        add: Iterable[int] = (),
        remove: Iterable[int] = (),
    ) -> None:
        pids = _clean_package_ids(package_ids)
        add_ids = frozenset(add)
        remove_ids = frozenset(remove)
        if add_ids & remove_ids:
            raise OrganizerError("A tag cannot be added and removed at once.")
        for tag_id in add_ids | remove_ids:
            self._tag(tag_id)
        if not pids or not (add_ids or remove_ids):
            return
        await self._db.set_mod_tags(pids, add_ids, remove_ids)
        await self._commit()

    # ── Rules ─────────────────────────────────────────────────────────────────

    async def set_rules(self, specs: Sequence[RuleSpec]) -> None:
        cleaned: list[RuleSpec] = []
        for spec in specs:
            if spec.field not in _RULE_FIELDS:
                raise OrganizerError(f'Unknown rule field "{spec.field}".')
            if spec.op not in _RULE_OPS:
                raise OrganizerError(f'Unknown rule operator "{spec.op}".')
            pattern = spec.pattern.strip()
            if not pattern:
                raise OrganizerError("Rule pattern cannot be empty.")
            if spec.folder_id == ROOT_ID:
                raise OrganizerError("Rules must target a folder, not the root.")
            self._folder(spec.folder_id)
            cleaned.append(msgspec.structs.replace(spec, pattern=pattern))
        await self._db.replace_rules(cleaned)
        await self._commit()

    async def add_standard_rules(self) -> int:
        """Append missing built-in rules, creating their top-level folders.

        Returns the number of rules added.
        """
        present = {
            (rule.field, rule.op, rule.pattern.casefold()) for rule in self.state.rules
        }
        existing = {folder.name.casefold() for folder in self._children(ROOT_ID)}
        rules: list[StandardRule] = []
        new_folders: dict[str, str] = {}
        for rule in STANDARD_RULES:
            key = (rule.field, rule.op, rule.pattern.casefold())
            if key in present:
                continue
            present.add(key)
            rules.append(rule)
            folder_key = rule.folder.casefold()
            if folder_key not in existing:
                new_folders.setdefault(folder_key, rule.folder)
        if not rules:
            return 0
        await self._db.append_named_rules(ROOT_ID, list(new_folders.values()), rules)
        await self._commit()
        return len(rules)

    # ── Read helpers ──────────────────────────────────────────────────────────

    def tree(self, query: TreeQuery | None = None) -> FolderNode:
        return build_tree(self.state, self._ctx.all_mods, self._ctx.active_uuids, query)

    def folder_mod_uuids(self, folder_id: int, recursive: bool = True) -> list[str]:
        """Mods under *folder_id*; ``recursive=False`` keeps only its direct mods."""
        self._folder(folder_id)
        for node in self.tree().walk():
            if node.folder.id == folder_id:
                leaves = node.all_mods() if recursive else node.mods
                return [leaf.uuid for leaf in leaves]
        return []

    def folder_toggle(
        self, folder_id: int, recursive: bool = True
    ) -> tuple[list[str], list[str]]:
        """(enable, disable) for a folder checkbox click.

        A fully enabled folder disables everything; partial or off enables all.
        """
        self._folder(folder_id)
        for node in self.tree().walk():
            if node.folder.id == folder_id:
                leaves = node.all_mods() if recursive else node.mods
                uuids = [leaf.uuid for leaf in leaves]
                check = node.check if recursive else node.own_check
                return ([], uuids) if check == "on" else (uuids, [])
        return [], []

    def folder_move_targets(self, folder_id: int) -> frozenset[int]:
        """Legal destinations, excluding the folder's current parent."""
        folder = self._non_root(folder_id)
        subtree = self._subtree_ids(folder_id)
        height = self._height(folder_id)
        key = folder.name.casefold()
        return frozenset(
            candidate.id
            for candidate in self.state.folders.values()
            if candidate.id not in subtree
            and candidate.id != folder.parent_id
            and self._depth(candidate.id) + height <= MAX_DEPTH
            and all(c.name.casefold() != key for c in self._children(candidate.id))
        )

    def tree_filters(self) -> list[TreeFilter]:
        return tree_filters(self._ctx.all_mods, self._ctx.active_uuids, self.state)
