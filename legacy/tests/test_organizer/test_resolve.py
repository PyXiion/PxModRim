from __future__ import annotations

from pxmodrim.core.models.metadata.structures import (
    AboutXmlMod,
    CaseInsensitiveStr,
    ListedMod,
)
from pxmodrim.core.organizer.models import (
    ROOT_ID,
    Folder,
    OrganizerState,
    Rule,
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
    folder_for,
    preview_rule_matches,
    tree_filters,
)

ROOT = Folder(id=ROOT_ID, parent_id=None, name="")


def _state(
    folders: list[Folder] | None = None,
    placements: dict[str, int] | None = None,
    rules: list[tuple[RuleField, RuleOp, str, int]] | None = None,
    mod_tags: dict[str, frozenset[int]] | None = None,
    tags: list[Tag] | dict[int, Tag] | None = None,
) -> OrganizerState:
    all_folders = {ROOT_ID: ROOT}
    for f in folders or []:
        all_folders[f.id] = f
    tag_dict: dict[int, Tag] = {}
    if isinstance(tags, dict):
        tag_dict = tags
    elif tags:
        tag_dict = {t.id: t for t in tags}
    return OrganizerState(
        folders=all_folders,
        placements=placements or {},
        tags=tag_dict,
        mod_tags=mod_tags or {},
        rules=tuple(
            Rule(id=i + 1, position=i, field=fld, op=op, pattern=pat, folder_id=fid)
            for i, (fld, op, pat, fid) in enumerate(rules or [])
        ),
    )


def _mod(
    uuid: str,
    name: str,
    pid: str | None = None,
    author: str = "",
    provider: str = "local",
) -> ListedMod:
    if pid is None:
        return ListedMod(name=name, _uuid=uuid, provider_id=provider)
    return AboutXmlMod(
        name=name,
        _uuid=uuid,
        provider_id=provider,
        package_id=CaseInsensitiveStr(pid),
        authors=[author] if author else [],
    )


def _find(root: FolderNode, folder_id: int) -> FolderNode:
    return next(n for n in root.walk() if n.folder.id == folder_id)


def test_rule_preview_first_match_and_current_ungrouped() -> None:
    state = _state(
        folders=[Folder(2, ROOT_ID, "A"), Folder(3, ROOT_ID, "B")],
        placements={"manual.mod": 3, "root.mod": ROOT_ID},
        rules=[("package_id", "prefix", "old.", 2)],
    )
    mods = {
        "a": _mod("a", "Core Alpha", "new.alpha", "Author"),
        "b": _mod("b", "Core Beta", "old.beta", "Other"),
        "c": _mod("c", "Core Manual", "manual.mod", "Author"),
        "d": _mod("d", "Core Ungrouped", "root.mod", "Author"),
        "e": _mod("e", "Core No ID"),
    }
    draft = [
        RuleSpec("author", "equals", "author", 3),
        RuleSpec("name", "contains", "core", 2),
    ]
    assert preview_rule_matches(state, mods, draft) == ([3, 1], 1)
    assert preview_rule_matches(state, mods, draft[::-1]) == ([4, 0], 1)
    assert preview_rule_matches(
        state, mods, [RuleSpec("name", "contains", "   ", 2), *draft]
    ) == ([0, 3, 1], 1)


class TestFolderFor:
    def test_manual_beats_rule(self) -> None:
        state = _state(
            folders=[Folder(2, ROOT_ID, "A"), Folder(3, ROOT_ID, "B")],
            placements={"x.mod": 3},
            rules=[("package_id", "prefix", "x.", 2)],
        )
        assert folder_for(state, "X.Mod", "Mod", "") == (3, "manual")

    def test_root_placement_blocks_rules(self) -> None:
        state = _state(
            folders=[Folder(2, ROOT_ID, "A")],
            placements={"x.mod": ROOT_ID},
            rules=[("package_id", "prefix", "x.", 2)],
        )
        assert folder_for(state, "x.mod", "Mod", "") == (ROOT_ID, "manual")

    def test_first_rule_by_position_wins(self) -> None:
        state = _state(
            folders=[Folder(2, ROOT_ID, "A"), Folder(3, ROOT_ID, "B")],
            rules=[
                ("author", "equals", "OSKAR", 3),
                ("name", "contains", "core", 2),
            ],
        )
        assert folder_for(state, "o.core", "Core Stuff", "Oskar") == (3, "rule")
        assert folder_for(state, "p.core", "Core Stuff", "Someone") == (2, "rule")

    def test_ops_are_case_insensitive(self) -> None:
        state = _state(
            folders=[Folder(2, ROOT_ID, "A")],
            rules=[("name", "equals", "hello world", 2)],
        )
        assert folder_for(state, "a.b", "Hello World", "") == (2, "rule")
        assert folder_for(state, "a.b", "Hello World!", "") == (ROOT_ID, "none")

    def test_no_package_id_is_ungrouped(self) -> None:
        state = _state(
            folders=[Folder(2, ROOT_ID, "A")],
            rules=[("name", "contains", "", 2)],
        )
        assert folder_for(state, None, "Anything", "") == (ROOT_ID, "none")

    def test_no_match_is_ungrouped(self) -> None:
        state = _state(
            folders=[Folder(2, ROOT_ID, "A")],
            rules=[("package_id", "prefix", "zzz", 2)],
        )
        assert folder_for(state, "a.b", "Mod", "") == (ROOT_ID, "none")


class TestBuildTree:
    def _nested(self) -> tuple[OrganizerState, dict[str, ListedMod]]:
        state = _state(
            folders=[
                Folder(2, ROOT_ID, "Zeta"),
                Folder(3, 2, "inner"),
                Folder(4, ROOT_ID, "alpha"),
            ],
            placements={"a.one": 2, "a.two": 3, "a.three": 3, "b.one": 4},
        )
        mods = {
            "u1": _mod("u1", "One", "a.one"),
            "u2": _mod("u2", "two", "a.two"),
            "u3": _mod("u3", "Three", "a.three"),
            "u4": _mod("u4", "B One", "b.one"),
            "u5": _mod("u5", "loose"),
        }
        return state, mods

    def test_tri_state_across_nested_folders(self) -> None:
        state, mods = self._nested()
        root = build_tree(state, mods, {"u1", "u2", "u3"})
        zeta, inner, alpha = _find(root, 2), _find(root, 3), _find(root, 4)
        assert (inner.total, inner.enabled, inner.check) == (2, 2, "on")
        assert (zeta.total, zeta.enabled, zeta.check) == (3, 3, "on")
        assert (alpha.total, alpha.enabled, alpha.check) == (1, 0, "off")
        assert (root.total, root.enabled, root.check) == (5, 3, "partial")

        root = build_tree(state, mods, ["u2"])
        assert _find(root, 3).check == "partial"
        assert _find(root, 2).check == "partial"

    def test_depths_and_root_holds_ungrouped(self) -> None:
        state, mods = self._nested()
        root = build_tree(state, mods, [])
        assert root.depth == 0
        assert _find(root, 2).depth == 1
        assert _find(root, 3).depth == 2
        assert [m.uuid for m in root.mods] == ["u5"]
        assert root.mods[0].placement == "none"
        assert root.mods[0].package_id is None

    def test_sorted_by_name_casefold(self) -> None:
        state, mods = self._nested()
        root = build_tree(state, mods, [])
        assert [c.folder.name for c in root.children] == ["alpha", "Zeta"]
        assert [m.name for m in _find(root, 3).mods] == ["Three", "two"]

    def test_query_prunes_but_counts_cover_all_mods(self) -> None:
        state, mods = self._nested()
        root = build_tree(state, mods, {"u1"}, TreeQuery(text="TWO"))
        assert [c.folder.id for c in root.children] == [2]
        zeta = _find(root, 2)
        assert zeta.mods == ()
        assert [m.uuid for m in _find(root, 3).mods] == ["u2"]
        assert (zeta.total, zeta.enabled, zeta.visible_total) == (3, 1, 1)
        assert (root.total, root.visible_total) == (5, 1)
        assert root.mods == ()

    def test_empty_query_does_not_prune(self) -> None:
        state = _state(folders=[Folder(2, ROOT_ID, "Empty")])
        root = build_tree(state, {}, [], TreeQuery())
        assert [c.folder.id for c in root.children] == [2]
        assert _find(root, 2).check == "off"

    def test_query_filters_status_tags_and_provider(self) -> None:
        state = _state(mod_tags={"a.one": frozenset({1, 2}), "a.two": frozenset({1})})
        mods = {
            "u1": _mod("u1", "One", "a.one", provider="steam"),
            "u2": _mod("u2", "Two", "a.two", author="Ludeon"),
            "u3": _mod("u3", "Three"),
        }

        def visible(query: TreeQuery) -> set[str]:
            root = build_tree(state, mods, {"u1"}, query)
            return {m.uuid for m in root.all_mods()}

        assert visible(TreeQuery(status="active")) == {"u1"}
        assert visible(TreeQuery(status="inactive")) == {"u2", "u3"}
        assert visible(TreeQuery(tag_ids=frozenset({1}))) == {"u1", "u2"}
        assert visible(TreeQuery(tag_ids=frozenset({1, 2}))) == {"u1"}
        assert visible(TreeQuery(provider_ids=frozenset({"steam"}))) == {"u1"}
        assert visible(TreeQuery(text="ludeon")) == {"u2"}
        assert visible(TreeQuery(text="a.one")) == {"u1"}

    def test_shared_package_id_shares_placement_and_tags(self) -> None:
        state = _state(
            folders=[Folder(2, ROOT_ID, "A")],
            placements={"dup.mod": 2},
            mod_tags={"dup.mod": frozenset({7})},
        )
        mods = {
            "u1": _mod("u1", "Copy 1", "Dup.Mod"),
            "u2": _mod("u2", "Copy 2", "dup.mod"),
        }
        folder = _find(build_tree(state, mods, []), 2)
        assert [m.uuid for m in folder.mods] == ["u1", "u2"]
        assert all(m.tag_ids == frozenset({7}) for m in folder.mods)

    def test_own_counts_and_rule_flag(self) -> None:
        state, mods = self._nested()
        state = _state(
            folders=list(state.folders.values())[1:],
            placements=state.placements,
            rules=[("name", "contains", "zzz", 3)],
        )
        root = build_tree(state, mods, {"u5", "u1"}, TreeQuery(text="nothing"))
        assert (root.own_total, root.own_enabled, root.own_check) == (1, 1, "on")
        assert (root.total, root.check) == (5, "partial")
        full = build_tree(state, mods, set())
        assert [n.folder.id for n in full.walk() if n.has_rules] == [3]
        assert _find(full, 2).own_total == 1


class TestTreeFilters:
    def test_query_includes_tag_id(self) -> None:
        tf = TreeFilter("tag:10", "Medieval", 3, tag_id=10)
        q = tf.query("sword")
        assert q.tag_ids == frozenset({10})
        assert q.text == "sword"
        assert q.status == "all"
        assert q.provider_ids == frozenset()
        assert not q.is_empty

    def test_tree_filters_preserves_order_and_counts_tags(self) -> None:
        state = _state(
            tags=[
                Tag(id=1, name="Medieval", color="#5eead4"),
                Tag(id=2, name="Combat", color="#fb923c"),
                Tag(id=3, name="Magic", color="#a3e635"),
            ],
            mod_tags={
                "a.one": frozenset({1, 2}),
                "a.two": frozenset({1}),
            },
        )
        mods = {
            "u1": _mod("u1", "One", "a.one", provider="steam"),
            "u2": _mod("u2", "Two", "a.two", provider="local"),
            "u3": _mod("u3", "Three", "a.three", provider="steam"),
        }
        filters = tree_filters(mods, {"u1"}, state)
        assert [f.key for f in filters[:3]] == ["all", "active", "inactive"]
        assert [f.count for f in filters[:3]] == [3, 1, 2]

        providers = [f for f in filters if f.provider_id is not None]
        assert [f.key for f in providers] == ["provider:local", "provider:steam"]
        assert [f.count for f in providers] == [1, 2]

        tag_filters = [f for f in filters if f.tag_id is not None]
        assert [f.key for f in tag_filters] == ["tag:2", "tag:3", "tag:1"]
        assert [f.label for f in tag_filters] == ["Combat", "Magic", "Medieval"]
        assert [f.count for f in tag_filters] == [1, 0, 2]

    def test_tree_filters_shared_package_ids_count_all_mods(self) -> None:
        state = _state(
            tags=[Tag(id=5, name="Overhaul", color="#5eead4")],
            mod_tags={"shared.mod": frozenset({5})},
        )
        mods = {
            "u1": _mod("u1", "Steam Copy", "Shared.Mod", provider="steam"),
            "u2": _mod("u2", "Local Copy", "shared.mod", provider="local"),
            "u3": _mod("u3", "Other Mod", "other.mod", provider="local"),
        }
        filters = tree_filters(mods, set(), state)
        tag_filter = next(f for f in filters if f.key == "tag:5")
        assert tag_filter.count == 2
        assert tag_filter.label == "Overhaul"
        assert tag_filter.tag_id == 5

    def test_tree_filters_without_state_or_without_tags(self) -> None:
        mods = {"u1": _mod("u1", "One", "a.one")}
        assert [f.key for f in tree_filters(mods, set(), None)] == [
            "all",
            "active",
            "inactive",
            "provider:local",
        ]
        assert [f.key for f in tree_filters(mods, set(), _state())] == [
            "all",
            "active",
            "inactive",
            "provider:local",
        ]

    def test_filtering_tree_with_tag_query(self) -> None:
        state = _state(
            tags=[
                Tag(id=1, name="Tagged", color="#5eead4"),
                Tag(id=2, name="Empty", color="#fb923c"),
            ],
            mod_tags={"a.one": frozenset({1}), "a.two": frozenset({1})},
        )
        mods = {
            "u1": _mod("u1", "Mod One", "a.one"),
            "u2": _mod("u2", "Mod Two", "a.two"),
            "u3": _mod("u3", "Mod Three", "a.three"),
        }
        filters = tree_filters(mods, set(), state)
        tag1_filter = next(f for f in filters if f.key == "tag:1")
        tree = build_tree(state, mods, set(), tag1_filter.query())
        assert {m.uuid for m in tree.all_mods()} == {"u1", "u2"}

        empty_filter = next(f for f in filters if f.key == "tag:2")
        empty_tree = build_tree(state, mods, set(), empty_filter.query())
        assert list(empty_tree.all_mods()) == []
        assert empty_tree.visible_total == 0

        narrowed = build_tree(state, mods, set(), tag1_filter.query("Two"))
        assert {m.uuid for m in narrowed.all_mods()} == {"u2"}
