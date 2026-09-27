from __future__ import annotations

from collections.abc import Callable, Collection, Iterator, Mapping
from typing import Literal

import msgspec

from pxmodrim.core.models.metadata.structures import AboutXmlMod, ListedMod
from pxmodrim.core.models.view.sidebar import PROVIDER_LABELS
from pxmodrim.core.organizer.models import ROOT_ID, Folder, OrganizerState, Rule

Placement = Literal["manual", "rule", "none"]
CheckState = Literal["on", "off", "partial"]
StatusFilter = Literal["all", "active", "inactive"]


class TreeQuery(msgspec.Struct, frozen=True):
    text: str = ""
    tag_ids: frozenset[int] = frozenset()
    status: StatusFilter = "all"
    provider_ids: frozenset[str] = frozenset()

    @property
    def is_empty(self) -> bool:
        return (
            not self.text.strip()
            and not self.tag_ids
            and self.status == "all"
            and not self.provider_ids
        )


class TreeFilter(msgspec.Struct, frozen=True):
    """A sidebar preset (status, provider, or tag) and the mods it selects."""

    key: str
    label: str
    count: int
    status: StatusFilter = "all"
    provider_id: str | None = None
    tag_id: int | None = None

    def query(self, text: str = "") -> TreeQuery:
        providers = (
            frozenset() if self.provider_id is None else frozenset((self.provider_id,))
        )
        tag_ids = frozenset() if self.tag_id is None else frozenset((self.tag_id,))
        return TreeQuery(
            text=text,
            tag_ids=tag_ids,
            status=self.status,
            provider_ids=providers,
        )


class ModLeaf(msgspec.Struct, frozen=True):
    uuid: str
    package_id: str | None
    name: str
    author: str
    enabled: bool
    tag_ids: frozenset[int]
    placement: Placement


class FolderNode(msgspec.Struct, frozen=True):
    folder: Folder
    depth: int
    children: tuple[FolderNode, ...]
    mods: tuple[ModLeaf, ...]
    total: int
    enabled: int
    check: CheckState
    visible_total: int
    own_total: int
    own_enabled: int
    has_rules: bool

    @property
    def own_check(self) -> CheckState:
        """Check state of the folder's direct mods only (the root's Ungrouped)."""
        return _check_state(self.own_total, self.own_enabled)

    def walk(self) -> Iterator[FolderNode]:
        yield self
        for child in self.children:
            yield from child.walk()

    def all_mods(self) -> Iterator[ModLeaf]:
        for node in self.walk():
            yield from node.mods


def mod_package_id(mod: ListedMod) -> str | None:
    if isinstance(mod, AboutXmlMod):
        return str(mod.package_id).lower()
    return None


def mod_author(mod: ListedMod) -> str:
    if isinstance(mod, AboutXmlMod):
        return ", ".join(mod.authors)
    return ""


def _rule_matcher(rule: Rule) -> Callable[[str], bool]:
    pattern = rule.pattern.casefold()
    if rule.op == "prefix":
        return lambda value: value.startswith(pattern)
    if rule.op == "contains":
        return lambda value: pattern in value
    return lambda value: value == pattern


class _Resolver:
    __slots__ = ("_placements", "_rules")

    def __init__(self, state: OrganizerState) -> None:
        self._placements = state.placements
        self._rules = [
            (rule.field, _rule_matcher(rule), rule.folder_id)
            for rule in state.rules
            if rule.folder_id in state.folders and rule.folder_id != ROOT_ID
        ]

    def resolve(
        self, package_id: str | None, name: str, author: str
    ) -> tuple[int, Placement]:
        if package_id is None:
            return ROOT_ID, "none"
        pid = package_id.lower()
        manual = self._placements.get(pid)
        if manual is not None:
            return manual, "manual"
        values = {
            "package_id": pid.casefold(),
            "name": name.casefold(),
            "author": author.casefold(),
        }
        for field, matches, folder_id in self._rules:
            if matches(values[field]):
                return folder_id, "rule"
        return ROOT_ID, "none"


def folder_for(
    state: OrganizerState, package_id: str | None, name: str, author: str
) -> tuple[int, Placement]:
    return _Resolver(state).resolve(package_id, name, author)


def _matches_query(leaf: ModLeaf, provider_id: str, query: TreeQuery) -> bool:
    if query.status == "active" and not leaf.enabled:
        return False
    if query.status == "inactive" and leaf.enabled:
        return False
    if query.provider_ids and provider_id not in query.provider_ids:
        return False
    if query.tag_ids and not query.tag_ids <= leaf.tag_ids:
        return False
    text = query.text.strip().casefold()
    if not text:
        return True
    return (
        text in leaf.name.strip().casefold()
        or (leaf.package_id is not None and text in leaf.package_id)
        or text in leaf.author.casefold()
    )


def _check_state(total: int, enabled: int) -> CheckState:
    if total and enabled == total:
        return "on"
    if enabled == 0:
        return "off"
    return "partial"


def _mod_sort_key(leaf: ModLeaf) -> tuple[str, str]:
    return leaf.name.casefold(), leaf.uuid


def _folder_sort_key(folder: Folder) -> tuple[str, int]:
    return folder.name.casefold(), folder.id


def tree_filters(
    mods: Mapping[str, ListedMod],
    active: Collection[str],
    state: OrganizerState | None = None,
) -> list[TreeFilter]:
    active_set = active if isinstance(active, (set, frozenset)) else set(active)
    enabled = sum(1 for uuid in mods if uuid in active_set)
    per_provider: dict[str, int] = {}
    for mod in mods.values():
        per_provider[mod.provider_id] = per_provider.get(mod.provider_id, 0) + 1
    filters = [
        TreeFilter("all", "All", len(mods)),
        TreeFilter("active", "Active", enabled, status="active"),
        TreeFilter("inactive", "Inactive", len(mods) - enabled, status="inactive"),
    ]
    labelled = sorted(
        (PROVIDER_LABELS.get(pid, pid.replace("_", " ").title()), pid)
        for pid in per_provider
    )
    filters.extend(
        TreeFilter(f"provider:{pid}", label, per_provider[pid], provider_id=pid)
        for label, pid in labelled
    )
    if state is not None and state.tags:
        tag_counts: dict[int, int] = dict.fromkeys(state.tags, 0)
        for mod in mods.values():
            pid = mod_package_id(mod)
            if pid and pid in state.mod_tags:
                for tid in state.mod_tags[pid]:
                    if tid in tag_counts:
                        tag_counts[tid] += 1
        sorted_tags = sorted(
            state.tags.values(), key=lambda t: (t.name.casefold(), t.id)
        )
        filters.extend(
            TreeFilter(
                key=f"tag:{tag.id}",
                label=tag.name,
                count=tag_counts[tag.id],
                tag_id=tag.id,
            )
            for tag in sorted_tags
        )
    return filters


def build_tree(
    state: OrganizerState,
    mods: Mapping[str, ListedMod],
    active: Collection[str],
    query: TreeQuery | None = None,
) -> FolderNode:
    filtering = query is not None and not query.is_empty
    active_set = active if isinstance(active, (set, frozenset)) else set(active)
    resolver = _Resolver(state)

    all_by_folder: dict[int, list[ModLeaf]] = {}
    visible_by_folder: dict[int, list[ModLeaf]] = {}
    for uuid, mod in mods.items():
        pid = mod_package_id(mod)
        author = mod_author(mod)
        folder_id, placement = resolver.resolve(pid, mod.name, author)
        if folder_id not in state.folders:
            folder_id, placement = ROOT_ID, "none"
        leaf = ModLeaf(
            uuid=uuid,
            package_id=pid,
            name=mod.name,
            author=author,
            enabled=uuid in active_set,
            tag_ids=state.mod_tags.get(pid, frozenset()) if pid else frozenset(),
            placement=placement,
        )
        all_by_folder.setdefault(folder_id, []).append(leaf)
        if (
            query is None
            or not filtering
            or _matches_query(leaf, mod.provider_id, query)
        ):
            visible_by_folder.setdefault(folder_id, []).append(leaf)

    children_of: dict[int, list[Folder]] = {}
    for folder in state.folders.values():
        if folder.parent_id is not None:
            children_of.setdefault(folder.parent_id, []).append(folder)
    ruled = {rule.folder_id for rule in state.rules}

    visited: set[int] = set()

    def build(folder: Folder, depth: int) -> FolderNode:
        visited.add(folder.id)
        children: list[FolderNode] = []
        total = 0
        enabled = 0
        visible_total = 0
        for child in sorted(children_of.get(folder.id, ()), key=_folder_sort_key):
            if child.id in visited:
                continue
            node = build(child, depth + 1)
            total += node.total
            enabled += node.enabled
            visible_total += node.visible_total
            if not filtering or node.visible_total:
                children.append(node)
        own = all_by_folder.get(folder.id, ())
        visible = sorted(visible_by_folder.get(folder.id, ()), key=_mod_sort_key)
        own_enabled = sum(1 for leaf in own if leaf.enabled)
        total += len(own)
        enabled += own_enabled
        visible_total += len(visible)
        return FolderNode(
            folder=folder,
            depth=depth,
            children=tuple(children),
            mods=tuple(visible),
            total=total,
            enabled=enabled,
            check=_check_state(total, enabled),
            visible_total=visible_total,
            own_total=len(own),
            own_enabled=own_enabled,
            has_rules=folder.id in ruled,
        )

    return build(state.folders[ROOT_ID], 0)
