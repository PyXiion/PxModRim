from __future__ import annotations

from unittest.mock import MagicMock

import pytest

from pxmodrim.core.config import AppConfig
from pxmodrim.core.context import CoreContext
from pxmodrim.core.models.metadata.structures import (
    AboutXmlMod,
    BaseRules,
    CaseInsensitiveStr,
    DependencyMod,
    ListedMod,
)
from pxmodrim.core.services.sort_service import SortService


def _mod(name: str, pid: str = "") -> AboutXmlMod:
    return AboutXmlMod(
        name=name,
        package_id=CaseInsensitiveStr(pid or name.lower().replace(" ", ".")),
        provider_id="stub",
        valid=True,
    )


def _mod_with_deps(name: str, pid: str, deps: dict[str, str]) -> AboutXmlMod:
    """Create an AboutXmlMod with the given dependencies.

    ``deps`` maps dependency package-id → dependency name.
    """
    return AboutXmlMod(
        name=name,
        package_id=CaseInsensitiveStr(pid),
        provider_id="stub",
        valid=True,
        about_rules=BaseRules(
            dependencies={
                CaseInsensitiveStr(dep_pid): DependencyMod(
                    name=dep_name,
                    package_id=CaseInsensitiveStr(dep_pid),
                )
                for dep_pid, dep_name in deps.items()
            }
        ),
    )


def _mod_with_alt_deps(
    name: str, pid: str, deps: dict[str, set[str]]
) -> AboutXmlMod:
    """Create an AboutXmlMod with deps that have alternative package IDs.

    ``deps`` maps dependency package-id → set of alternative package IDs.
    """
    return AboutXmlMod(
        name=name,
        package_id=CaseInsensitiveStr(pid),
        provider_id="stub",
        valid=True,
        about_rules=BaseRules(
            dependencies={
                CaseInsensitiveStr(dep_pid): DependencyMod(
                    name=f"Dep of {name}",
                    package_id=CaseInsensitiveStr(dep_pid),
                    alternative_package_ids={
                        CaseInsensitiveStr(a) for a in alts
                    },
                )
                for dep_pid, alts in deps.items()
            }
        ),
    )


@pytest.fixture
def ctx() -> CoreContext:
    return CoreContext(AppConfig())


@pytest.fixture
def sort_service(ctx: CoreContext) -> SortService:
    return SortService(ctx, MagicMock())


class TestResolveMissingDependencies:
    def test_no_dependencies(self, sort_service: SortService) -> None:
        sort_service._ctx.load(
            {
                "uuid-a": _mod("Mod A", "mod.a"),
                "uuid-b": _mod("Mod B", "mod.b"),
            },
            active_uuids=["uuid-a", "uuid-b"],
        )
        result = sort_service.resolve_missing_dependencies({"uuid-a", "uuid-b"})
        assert result == []

    def test_missing_dep_exists_as_inactive(
        self, sort_service: SortService
    ) -> None:
        sort_service._ctx.load(
            {
                "uuid-a": _mod_with_deps("Mod A", "mod.a", {"mod.b": "Mod B"}),
                "uuid-b": _mod("Mod B", "mod.b"),
            },
            active_uuids=["uuid-a"],
        )
        result = sort_service.resolve_missing_dependencies({"uuid-a"})
        assert result == ["uuid-b"]

    def test_missing_dep_not_in_all_mods(
        self, sort_service: SortService
    ) -> None:
        sort_service._ctx.load(
            {
                "uuid-a": _mod_with_deps(
                    "Mod A", "mod.a", {"mod.b": "Mod B"}
                ),
            },
            active_uuids=["uuid-a"],
        )
        result = sort_service.resolve_missing_dependencies({"uuid-a"})
        assert result == []

    def test_missing_dep_already_active(
        self, sort_service: SortService
    ) -> None:
        sort_service._ctx.load(
            {
                "uuid-a": _mod_with_deps("Mod A", "mod.a", {"mod.b": "Mod B"}),
                "uuid-b": _mod("Mod B", "mod.b"),
            },
            active_uuids=["uuid-a", "uuid-b"],
        )
        result = sort_service.resolve_missing_dependencies(
            {"uuid-a", "uuid-b"}
        )
        assert result == []

    def test_dep_satisfied_via_alternative_pid(
        self, sort_service: SortService
    ) -> None:
        sort_service._ctx.load(
            {
                "uuid-a": _mod_with_alt_deps(
                    "Mod A",
                    "mod.a",
                    {"mod.b": {"mod.c"}},
                ),
                "uuid-c": _mod("Mod C", "mod.c"),
            },
            active_uuids=["uuid-a", "uuid-c"],
        )
        result = sort_service.resolve_missing_dependencies(
            {"uuid-a", "uuid-c"}
        )
        assert result == []

    def test_non_about_xml_mod_in_active_set_skipped(
        self, sort_service: SortService
    ) -> None:
        folder_mod = ListedMod(name="Folder Mod", provider_id="stub")
        sort_service._ctx.load(
            {
                "uuid-folder": folder_mod,
                "uuid-a": _mod("Mod A", "mod.a"),
            },
            active_uuids=["uuid-folder", "uuid-a"],
        )
        result = sort_service.resolve_missing_dependencies(
            {"uuid-folder", "uuid-a"}
        )
        assert result == []

    def test_no_duplicates_when_two_mods_share_missing_dep(
        self, sort_service: SortService
    ) -> None:
        sort_service._ctx.load(
            {
                "uuid-a": _mod_with_deps("Mod A", "mod.a", {"mod.c": "Mod C"}),
                "uuid-b": _mod_with_deps("Mod B", "mod.b", {"mod.c": "Mod C"}),
                "uuid-c": _mod("Mod C", "mod.c"),
            },
            active_uuids=["uuid-a", "uuid-b"],
        )
        result = sort_service.resolve_missing_dependencies(
            {"uuid-a", "uuid-b"}
        )
        assert result == ["uuid-c"]

    def test_empty_active_set_returns_empty(
        self, sort_service: SortService
    ) -> None:
        sort_service._ctx.load(
            {
                "uuid-a": _mod_with_deps("Mod A", "mod.a", {"mod.b": "Mod B"}),
                "uuid-b": _mod("Mod B", "mod.b"),
            },
            active_uuids=[],
        )
        result = sort_service.resolve_missing_dependencies(set())
        assert result == []

    def test_mod_with_no_deps_does_not_add_to_result(
        self, sort_service: SortService
    ) -> None:
        sort_service._ctx.load(
            {
                "uuid-a": _mod("Mod A", "mod.a"),
                "uuid-b": _mod_with_deps("Mod B", "mod.b", {"mod.a": "Mod A"}),
            },
            active_uuids=["uuid-a", "uuid-b"],
        )
        result = sort_service.resolve_missing_dependencies(
            {"uuid-a", "uuid-b"}
        )
        assert result == []
