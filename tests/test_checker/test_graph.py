from __future__ import annotations

from pxmodrim.checker.graph import (
    ConstraintGraph,
    EdgeOrigin,
    EdgeType,
    PackageId,
)
from pxmodrim.models.metadata.structures import (
    AboutXmlMod,
    CaseInsensitiveSet,
    CaseInsensitiveStr,
    DependencyMod,
)
from pxmodrim.sort.config import SortSettings


def _make_mod(package_id: str) -> AboutXmlMod:
    m = AboutXmlMod()
    m.package_id = CaseInsensitiveStr(package_id)
    m.name = package_id
    m._mod_path = None
    return m


def _settings() -> SortSettings:
    return SortSettings(
        use_community_rules=False,
        use_moddependencies_as_load_before=False,
        use_alternative_package_ids=False,
    )


class TestConstraintGraph:
    def test_empty_graph(self):
        g = ConstraintGraph()
        assert len(g.nodes) == 0

    def test_build_no_edges(self):
        g = ConstraintGraph()
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b}
        g.build(mods, [PackageId("mod.a"), PackageId("mod.b")], _settings())
        assert PackageId("mod.a") in g.nodes
        assert PackageId("mod.b") in g.nodes
        assert len(g.nodes) == 2
        assert g.index_of(PackageId("mod.a")) == 0
        assert g.index_of(PackageId("mod.b")) == 1

    def test_dependency_edge(self):
        g = ConstraintGraph()
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        dep = DependencyMod()
        dep.package_id = CaseInsensitiveStr("mod.b")
        a.about_rules.dependencies = {PackageId("mod.b"): dep}
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b}
        g.build(mods, [PackageId("mod.a"), PackageId("mod.b")], _settings())

        edges = g.edges_of_type(PackageId("mod.a"), EdgeType.DEPENDENCY)
        assert len(edges) == 1
        edge = next(iter(edges))
        assert edge.source == PackageId("mod.a")
        assert edge.target == PackageId("mod.b")
        assert edge.type == EdgeType.DEPENDENCY
        assert edge.origin == EdgeOrigin.ABOUT_XML

    def test_incompatibility_edge(self):
        g = ConstraintGraph()
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        a.about_rules.incompatible_with = CaseInsensitiveSet(["mod.b"])
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b}
        g.build(mods, [PackageId("mod.a"), PackageId("mod.b")], _settings())

        edges = g.edges_of_type(PackageId("mod.a"), EdgeType.INCOMPATIBILITY)
        assert len(edges) == 1
        assert next(iter(edges)).target == PackageId("mod.b")

    def test_load_after_edge(self):
        g = ConstraintGraph()
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        a.about_rules.load_after = CaseInsensitiveSet(["mod.b"])
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b}
        g.build(mods, [PackageId("mod.a"), PackageId("mod.b")], _settings())

        edges = g.edges_of_type(PackageId("mod.a"), EdgeType.LOAD_AFTER)
        assert len(edges) == 1
        assert next(iter(edges)).target == PackageId("mod.b")

    def test_load_before_edge(self):
        g = ConstraintGraph()
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        a.about_rules.load_before = CaseInsensitiveSet(["mod.b"])
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b}
        g.build(mods, [PackageId("mod.a"), PackageId("mod.b")], _settings())

        edges = g.edges_of_type(PackageId("mod.a"), EdgeType.LOAD_BEFORE)
        assert len(edges) == 1
        assert next(iter(edges)).target == PackageId("mod.b")

    def test_neighbors(self):
        g = ConstraintGraph()
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        a.about_rules.incompatible_with = CaseInsensitiveSet(["mod.b"])
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b}
        g.build(mods, [PackageId("mod.a"), PackageId("mod.b")], _settings())

        nbrs = g.neighbors(PackageId("mod.a"))
        assert PackageId("mod.b") in nbrs
        nbrs_b = g.neighbors(PackageId("mod.b"))
        assert PackageId("mod.a") in nbrs_b

    def test_incoming(self):
        g = ConstraintGraph()
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        a.about_rules.incompatible_with = CaseInsensitiveSet(["mod.b"])
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b}
        g.build(mods, [PackageId("mod.a"), PackageId("mod.b")], _settings())

        incoming = g.incoming(PackageId("mod.b"))
        assert len(incoming) == 1
        assert next(iter(incoming)).source == PackageId("mod.a")

    def test_add_remove_node(self):
        g = ConstraintGraph()
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        a.about_rules.incompatible_with = CaseInsensitiveSet(["mod.b"])
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b}
        g.build(mods, [PackageId("mod.a"), PackageId("mod.b")], _settings())
        assert PackageId("mod.b") in g.nodes

        g.remove_mod(PackageId("mod.b"))
        assert PackageId("mod.b") not in g.nodes
        # Incoming edge from 'a' should be gone too
        assert len(g.incoming(PackageId("mod.a"))) == 0

    def test_update_order(self):
        g = ConstraintGraph()
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b}
        g.build(mods, [PackageId("mod.a"), PackageId("mod.b")], _settings())
        assert g.index_of(PackageId("mod.a")) == 0
        assert g.index_of(PackageId("mod.b")) == 1

        g.update_order([PackageId("mod.b"), PackageId("mod.a")])
        assert g.index_of(PackageId("mod.a")) == 1
        assert g.index_of(PackageId("mod.b")) == 0

    def test_cycle_detection(self):
        g = ConstraintGraph()
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        c = _make_mod("mod.c")
        a.about_rules.load_after = CaseInsensitiveSet(["mod.b"])
        b.about_rules.load_after = CaseInsensitiveSet(["mod.c"])
        c.about_rules.load_after = CaseInsensitiveSet(["mod.a"])
        mods = {
            PackageId("mod.a"): a,
            PackageId("mod.b"): b,
            PackageId("mod.c"): c,
        }
        g.build(mods, list(mods.keys()), _settings())
        cycles = g.find_cycles()
        assert len(cycles) >= 1
        cycle_pids = {str(p) for cycle in cycles for p in cycle}
        for pid in ("mod.a", "mod.b", "mod.c"):
            assert pid in cycle_pids

    def test_no_cycle(self):
        g = ConstraintGraph()
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        a.about_rules.load_after = CaseInsensitiveSet(["mod.b"])
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b}
        g.build(mods, list(mods.keys()), _settings())
        cycles = g.find_cycles()
        assert len(cycles) == 0
