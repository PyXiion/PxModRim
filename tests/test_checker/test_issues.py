from __future__ import annotations

from pxmodrim.checker.graph import ConstraintGraph
from pxmodrim.checker.issues import (
    CycleIssueChecker,
    DependencyIssueChecker,
    GameVersionIssueChecker,
    IncompatibilityIssueChecker,
    LoadOrderIssueChecker,
    ReplacementIssueChecker,
)
from pxmodrim.checker.models import CheckContext, PackageId
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
        use_alternative_package_ids=True,
        check_missing_dependencies=True,
    )


def _ctx(
    mods: dict[PackageId, AboutXmlMod],
    ordered_pids: list[PackageId] | None = None,
    graph: ConstraintGraph | None = None,
    settings: SortSettings | None = None,
    **kwargs: bool | str | set | list | dict,
) -> CheckContext:
    if ordered_pids is None:
        ordered_pids = list(mods.keys())
    if graph is None:
        graph = ConstraintGraph()
        graph.build(mods, ordered_pids, settings or _settings())
    pid_to_index = {p: i for i, p in enumerate(ordered_pids)}
    return CheckContext(
        active_mods=mods,
        ordered_pids=ordered_pids,
        pid_to_index=pid_to_index,
        graph=graph,
        settings=settings or _settings(),
        **kwargs,
    )


class TestDependencyIssueChecker:
    def test_no_issues_no_deps(self):
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b}
        ctx = _ctx(mods)
        checker = DependencyIssueChecker()
        assert not checker.should_check(a, ctx)
        assert checker.check(a, ctx) == []

    def test_missing_dep(self):
        a = _make_mod("mod.a")
        dep = DependencyMod()
        dep.package_id = CaseInsensitiveStr("mod.b")
        a.about_rules.dependencies = {PackageId("mod.b"): dep}
        # b is not added to active_mods — mimics missing dependency
        ctx = CheckContext(
            active_mods={PackageId("mod.a"): a},
            ordered_pids=[PackageId("mod.a")],
            pid_to_index={PackageId("mod.a"): 0},
            graph=ConstraintGraph(),
            settings=_settings(),
        )
        checker = DependencyIssueChecker()
        assert checker.should_check(a, ctx)
        issues = checker.check(a, ctx)
        assert len(issues) == 1
        assert issues[0].category == "missing_dependency"
        assert issues[0].severity == "error"

    def test_dep_satisfied(self):
        a = _make_mod("mod.a")
        dep = DependencyMod()
        dep.package_id = CaseInsensitiveStr("mod.b")
        a.about_rules.dependencies = {PackageId("mod.b"): dep}
        b = _make_mod("mod.b")
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b}
        ctx = _ctx(mods)
        checker = DependencyIssueChecker()
        assert checker.check(a, ctx) == []


class TestIncompatibilityIssueChecker:
    def test_no_incompatibilities(self):
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b}
        ctx = _ctx(mods)
        checker = IncompatibilityIssueChecker()
        assert checker.check(a, ctx) == []

    def test_self_declared(self):
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        a.about_rules.incompatible_with = CaseInsensitiveSet(["mod.b"])
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b}
        ctx = _ctx(mods)
        checker = IncompatibilityIssueChecker()
        issues = checker.check(a, ctx)
        assert len(issues) == 1
        assert issues[0].category == "incompatibility"
        assert issues[0].severity == "error"


class TestLoadOrderIssueChecker:
    def test_no_violations(self):
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        a.about_rules.load_after = CaseInsensitiveSet(["mod.b"])
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b}
        ordered = [PackageId("mod.b"), PackageId("mod.a")]
        ctx = _ctx(mods, ordered_pids=ordered)
        checker = LoadOrderIssueChecker()
        issues = checker.check(a, ctx)
        assert issues == []

    def test_load_after_violation(self):
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        a.about_rules.load_after = CaseInsensitiveSet(["mod.b"])
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b}
        ordered = [PackageId("mod.a"), PackageId("mod.b")]
        ctx = _ctx(mods, ordered_pids=ordered)
        checker = LoadOrderIssueChecker()
        issues = checker.check(a, ctx)
        assert len(issues) == 1
        assert issues[0].category == "load_order"
        assert issues[0].severity == "warning"

    def test_load_before_violation(self):
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        a.about_rules.load_before = CaseInsensitiveSet(["mod.b"])
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b}
        # A should load before B but appears after B
        ordered = [PackageId("mod.b"), PackageId("mod.a")]
        ctx = _ctx(mods, ordered_pids=ordered)
        checker = LoadOrderIssueChecker()
        issues = checker.check(a, ctx)
        assert len(issues) == 1
        assert issues[0].category == "load_order"
        assert issues[0].severity == "warning"


class TestCycleIssueChecker:
    def test_no_cycle(self):
        a = _make_mod("mod.a")
        mods = {PackageId("mod.a"): a}
        ctx = _ctx(mods, cycles=[])
        checker = CycleIssueChecker()
        assert checker.check(a, ctx) == []

    def test_cycle_member(self):
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b}
        ctx = _ctx(
            mods,
            cycles=[
                [PackageId("mod.a"), PackageId("mod.b")],
            ],
        )
        checker = CycleIssueChecker()
        issues = checker.check(a, ctx)
        assert len(issues) == 1
        assert issues[0].category == "cycle"
        assert issues[0].severity == "warning"

    def test_non_member_no_issue(self):
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        c = _make_mod("mod.c")
        mods = {PackageId("mod.a"): a, PackageId("mod.b"): b, PackageId("mod.c"): c}
        ctx = _ctx(
            mods,
            cycles=[
                [PackageId("mod.a"), PackageId("mod.b")],
            ],
        )
        checker = CycleIssueChecker()
        assert checker.check(c, ctx) == []


class TestGameVersionIssueChecker:
    def test_no_versions_no_issue(self):
        a = _make_mod("mod.a")
        a.supported_versions = set()
        mods = {PackageId("mod.a"): a}
        ctx = _ctx(mods)
        checker = GameVersionIssueChecker()
        assert not checker.should_check(a, ctx)
        assert checker.check(a, ctx) == []

    def test_version_match(self):
        a = _make_mod("mod.a")
        a.supported_versions = {"1.5"}
        mods = {PackageId("mod.a"): a}
        ctx = _ctx(mods, target_version="1.5")
        checker = GameVersionIssueChecker()
        assert checker.check(a, ctx) == []

    def test_version_mismatch(self):
        a = _make_mod("mod.a")
        a.supported_versions = {"1.4"}
        mods = {PackageId("mod.a"): a}
        ctx = _ctx(mods, target_version="1.5")
        checker = GameVersionIssueChecker()
        issues = checker.check(a, ctx)
        assert len(issues) == 1
        assert issues[0].category == "version_mismatch"
        assert issues[0].severity == "warning"

    def test_no_version_warning_suppressed(self):
        a = _make_mod("mod.a")
        a.supported_versions = {"1.4"}
        mods = {PackageId("mod.a"): a}
        ctx = _ctx(mods, target_version="1.5", no_version_warning={PackageId("mod.a")})
        checker = GameVersionIssueChecker()
        assert checker.check(a, ctx) == []


class TestReplacementIssueChecker:
    def test_no_replacement(self):
        a = _make_mod("mod.a")
        mods = {PackageId("mod.a"): a}
        ctx = _ctx(mods)
        checker = ReplacementIssueChecker()
        assert checker.check(a, ctx) == []

    def test_replacement_available(self):
        a = _make_mod("mod.a")
        a._mod_path = None  # No published_file_id
        mods = {PackageId("mod.a"): a}
        ctx = _ctx(mods)
        checker = ReplacementIssueChecker()
        assert checker.check(a, ctx) == []
