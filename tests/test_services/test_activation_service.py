from __future__ import annotations

from pathlib import Path

import pytest

from pxmodrim.core.config import AppConfig, ConfigService
from pxmodrim.core.context import CoreContext
from pxmodrim.core.models.metadata.structures import (
    AboutXmlMod,
    BaseRules,
    CaseInsensitiveStr,
    DependencyMod,
    ListedMod,
)
from pxmodrim.core.services.activation_service import ActivationService
from pxmodrim.core.services.diagnostics_service import DiagnosticsService


def _mod(name: str, pid: str, *deps: str) -> AboutXmlMod:
    return AboutXmlMod(
        name=name,
        package_id=CaseInsensitiveStr(pid),
        provider_id="stub",
        valid=True,
        about_rules=BaseRules(
            dependencies={
                CaseInsensitiveStr(dep): DependencyMod(
                    name=dep, package_id=CaseInsensitiveStr(dep)
                )
                for dep in deps
            }
        ),
    )


_MODS: dict[str, ListedMod] = {
    "uuid-a": _mod("Mod A", "mod.a"),
    "uuid-b": _mod("Mod B", "mod.b", "mod.a"),
    "uuid-c": _mod("Mod C", "mod.c", "mod.b"),
    "uuid-d": _mod("Mod D", "mod.d"),
    "uuid-e": _mod("Mod E", "mod.e", "mod.a"),
    "uuid-f": _mod("Mod F", "mod.f"),
}


@pytest.fixture
def ctx(tmp_path: Path) -> CoreContext:
    ctx = CoreContext(AppConfig(), ConfigService(tmp_path))
    ctx._diagnostics_service = DiagnosticsService(ctx)
    ctx.load(_MODS, ["uuid-c", "uuid-d", "uuid-a", "uuid-b"])
    ctx.diagnostics_service.rebuild()
    return ctx


@pytest.fixture
def activation(ctx: CoreContext) -> ActivationService:
    return ActivationService(ctx)


class TestSetEnabled:
    def test_enabling_appends_without_reordering(
        self, ctx: CoreContext, activation: ActivationService
    ) -> None:
        emitted: list[tuple[str, ...]] = []
        ctx.active_state_changed.connect(emitted.append)

        assert activation.set_enabled(["uuid-f", "uuid-e"], True) is True

        expected = ["uuid-c", "uuid-d", "uuid-a", "uuid-b", "uuid-f", "uuid-e"]
        assert ctx.active_uuids == expected
        assert emitted == [tuple(expected)]

    def test_enabling_already_active_mod_keeps_its_position(
        self, ctx: CoreContext, activation: ActivationService
    ) -> None:
        assert activation.set_enabled(["uuid-c", "uuid-f"], True) is True

        assert ctx.active_uuids == ["uuid-c", "uuid-d", "uuid-a", "uuid-b", "uuid-f"]

    def test_disabling_removes_and_keeps_remaining_order(
        self, ctx: CoreContext, activation: ActivationService
    ) -> None:
        assert activation.set_enabled(["uuid-d", "uuid-a"], False) is True

        assert ctx.active_uuids == ["uuid-c", "uuid-b"]

    def test_unknown_ids_are_ignored(
        self, ctx: CoreContext, activation: ActivationService
    ) -> None:
        assert activation.set_enabled(["uuid-missing", "uuid-f"], True) is True

        assert ctx.active_uuids == ["uuid-c", "uuid-d", "uuid-a", "uuid-b", "uuid-f"]

    @pytest.mark.parametrize(
        ("uuids", "enabled"),
        [
            (["uuid-a", "uuid-c"], True),
            (["uuid-e", "uuid-f"], False),
            (["uuid-missing"], True),
            (["uuid-missing"], False),
            ([], True),
        ],
    )
    def test_noop_returns_false_without_emitting(
        self,
        ctx: CoreContext,
        activation: ActivationService,
        uuids: list[str],
        enabled: bool,
    ) -> None:
        emitted: list[tuple[str, ...]] = []
        ctx.active_state_changed.connect(emitted.append)

        assert activation.set_enabled(uuids, enabled) is False

        assert ctx.active_uuids == ["uuid-c", "uuid-d", "uuid-a", "uuid-b"]
        assert emitted == []


class TestDependentsOf:
    def test_transitive_dependents_in_active_order(
        self, activation: ActivationService
    ) -> None:
        assert activation.dependents_of(["uuid-a"]) == ["uuid-c", "uuid-b"]

    def test_excludes_inputs(self, activation: ActivationService) -> None:
        assert activation.dependents_of(["uuid-a", "uuid-b"]) == ["uuid-c"]

    def test_excludes_inactive_dependents(
        self, ctx: CoreContext, activation: ActivationService
    ) -> None:
        assert "uuid-e" not in ctx.active_uuids

        assert "uuid-e" not in activation.dependents_of(["uuid-a"])

    def test_reflects_graph_after_state_change(
        self, activation: ActivationService
    ) -> None:
        activation.set_enabled(["uuid-e"], True)

        assert activation.dependents_of(["uuid-a"]) == ["uuid-c", "uuid-b", "uuid-e"]

    def test_mod_without_dependents(self, activation: ActivationService) -> None:
        assert activation.dependents_of(["uuid-d", "uuid-missing"]) == []
