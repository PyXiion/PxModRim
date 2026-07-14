from __future__ import annotations

from pxmodrim.core.checker.checker import ModChecker
from pxmodrim.core.checker.issues import (
    CycleIssueChecker,
    DependencyIssueChecker,
    IncompatibilityIssueChecker,
    LoadOrderIssueChecker,
    ModIssueChecker,
)
from pxmodrim.core.checker.models import ModDiagnostics
from pxmodrim.core.models.metadata.structures import (
    AboutXmlMod,
    CaseInsensitiveSet,
    CaseInsensitiveStr,
    DependencyMod,
    ListedMod,
)
from pxmodrim.core.sort.config import SortSettings


def _make_mod(package_id: str, name: str | None = None) -> AboutXmlMod:
    m = AboutXmlMod()
    m.package_id = CaseInsensitiveStr(package_id)
    m.name = name or package_id
    m._mod_path = None
    return m


def _settings() -> SortSettings:
    return SortSettings(
        use_community_rules=False,
        use_alternative_package_ids=True,
        check_missing_dependencies=True,
    )


def _as_listed(mods: list[AboutXmlMod]) -> dict[str, ListedMod]:
    return {str(i): m for i, m in enumerate(mods)}


def _uuid_for(mods: dict[str, ListedMod], name: str) -> str:
    for u, m in mods.items():
        if m.name == name:
            return u
    raise ValueError(f"Mod {name} not found")


class TestModChecker:
    def test_basic_rebuild(self):
        checkers = [
            DependencyIssueChecker(),
            IncompatibilityIssueChecker(),
            LoadOrderIssueChecker(),
            CycleIssueChecker(),
        ]
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        dep = DependencyMod()
        dep.package_id = CaseInsensitiveStr("mod.c")
        a.about_rules.dependencies = {CaseInsensitiveStr("mod.c"): dep}
        mods = _as_listed([a, b])
        active_uuids = [u for u, m in mods.items() if m.name in ("mod.a", "mod.b")]

        collected_diags: dict[str, ModDiagnostics] = {}

        def on_change(diags: dict[str, ModDiagnostics]) -> None:
            collected_diags.update(diags)

        checker = ModChecker(checkers, _settings(), on_diagnostics_changed=on_change)
        checker.rebuild(mods, active_uuids)

        # 'a' has a missing dependency on 'mod.c'
        a_uuid = _uuid_for(mods, "mod.a")
        diag_a = checker.diagnostics_for(a_uuid)
        assert diag_a is not None
        assert diag_a.has_errors
        assert len(diag_a.errors) == 1
        assert diag_a.errors[0].category == "missing_dependency"

        # 'b' has no issues
        b_uuid = _uuid_for(mods, "mod.b")
        diag_b = checker.diagnostics_for(b_uuid)
        assert diag_b is not None
        assert not diag_b.has_errors
        assert not diag_b.has_warnings

    def test_toggle_mod(self):
        checkers = [
            DependencyIssueChecker(),
            IncompatibilityIssueChecker(),
        ]
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        dep = DependencyMod()
        dep.package_id = CaseInsensitiveStr("mod.b")
        a.about_rules.dependencies = {CaseInsensitiveStr("mod.b"): dep}
        mods = _as_listed([a, b])
        active_uuids = [u for u, m in mods.items() if m.name == "mod.a"]

        checker = ModChecker(checkers, _settings())
        checker.rebuild(mods, active_uuids)

        a_uuid = _uuid_for(mods, "mod.a")
        b_uuid = _uuid_for(mods, "mod.b")

        # 'a' should have missing dependency error (b is not active)
        diag = checker.diagnostics_for(a_uuid)
        assert diag is not None and diag.has_errors

        # Toggle b on
        all_uuids = [a_uuid, b_uuid]
        checker.toggle_mod(mods[b_uuid], True, all_uuids)

        # Now dep should be satisfied
        diag = checker.diagnostics_for(a_uuid)
        assert diag is None or not diag.has_errors

    def test_move_mod_updates_load_order(self):
        checkers: list[ModIssueChecker] = [
            LoadOrderIssueChecker(),
        ]
        a = _make_mod("mod.a")
        b = _make_mod("mod.b")
        a.about_rules.load_after = CaseInsensitiveSet(["mod.b"])
        # Order: b first, then a -> satisfies load_after
        mods = _as_listed([b, a])
        active_uuids = list(mods)

        checker = ModChecker(checkers, _settings())
        checker.rebuild(mods, active_uuids)

        a_uuid = _uuid_for(mods, "mod.a")

        # Initially b is before a, so no load_after violation for a
        diag = checker.diagnostics_for(a_uuid)
        assert diag is None or not diag.has_warnings

        # Move a before b
        checker.move_mod(a_uuid, 0, 1)

        # Now a is before b, violating load_after
        diag = checker.diagnostics_for(a_uuid)
        assert diag is not None and diag.has_warnings
        assert diag.warnings[0].category == "load_order"
