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


class TestApply:
    def test_enabling_appends_without_reordering(
        self, ctx: CoreContext, activation: ActivationService
    ) -> None:
        emitted: list[tuple[str, ...]] = []
        ctx.active_state_changed.connect(emitted.append)

        assert activation.apply(enable=["uuid-f", "uuid-e"]) is True

        expected = ["uuid-c", "uuid-d", "uuid-a", "uuid-b", "uuid-f", "uuid-e"]
        assert ctx.active_uuids == expected
        assert emitted == [tuple(expected)]

    def test_enabling_already_active_mod_keeps_its_position(
        self, ctx: CoreContext, activation: ActivationService
    ) -> None:
        assert activation.apply(enable=["uuid-c", "uuid-f"]) is True

        assert ctx.active_uuids == ["uuid-c", "uuid-d", "uuid-a", "uuid-b", "uuid-f"]

    def test_disabling_removes_and_keeps_remaining_order(
        self, ctx: CoreContext, activation: ActivationService
    ) -> None:
        assert activation.apply(disable=["uuid-d", "uuid-a"]) is True

        assert ctx.active_uuids == ["uuid-c", "uuid-b"]

    def test_unknown_ids_are_ignored(
        self, ctx: CoreContext, activation: ActivationService
    ) -> None:
        assert activation.apply(enable=["uuid-missing", "uuid-f"]) is True

        assert ctx.active_uuids == ["uuid-c", "uuid-d", "uuid-a", "uuid-b", "uuid-f"]

    def test_mixed_selection_applies_in_single_emission(
        self, ctx: CoreContext, activation: ActivationService
    ) -> None:
        emitted: list[tuple[str, ...]] = []
        ctx.active_state_changed.connect(emitted.append)

        assert (
            activation.apply(
                enable=["uuid-f", "uuid-e"],
                disable=["uuid-a", "uuid-d"],
            )
            is True
        )

        expected = ["uuid-c", "uuid-b", "uuid-f", "uuid-e"]
        assert ctx.active_uuids == expected
        assert emitted == [tuple(expected)]

    def test_uuid_in_both_enable_and_disable_favors_disable(
        self, ctx: CoreContext, activation: ActivationService
    ) -> None:
        emitted: list[tuple[str, ...]] = []
        ctx.active_state_changed.connect(emitted.append)

        assert (
            activation.apply(
                enable=["uuid-a", "uuid-f", "uuid-e"],
                disable=["uuid-a", "uuid-f"],
            )
            is True
        )

        expected = ["uuid-c", "uuid-d", "uuid-b", "uuid-e"]
        assert ctx.active_uuids == expected
        assert emitted == [tuple(expected)]

    def test_duplicate_enable_deduped_in_input_order(
        self, ctx: CoreContext, activation: ActivationService
    ) -> None:
        assert activation.apply(enable=["uuid-f", "uuid-e", "uuid-f"]) is True
        assert ctx.active_uuids == [
            "uuid-c",
            "uuid-d",
            "uuid-a",
            "uuid-b",
            "uuid-f",
            "uuid-e",
        ]

    @pytest.mark.parametrize(
        ("enable", "disable"),
        [
            (["uuid-a", "uuid-c"], []),
            ([], ["uuid-e", "uuid-f"]),
            (["uuid-missing"], []),
            ([], ["uuid-missing"]),
            ([], []),
            (["uuid-missing"], ["uuid-missing"]),
        ],
    )
    def test_noop_returns_false_without_emitting(
        self,
        ctx: CoreContext,
        activation: ActivationService,
        enable: list[str],
        disable: list[str],
    ) -> None:
        emitted: list[tuple[str, ...]] = []
        ctx.active_state_changed.connect(emitted.append)

        assert activation.apply(enable=enable, disable=disable) is False

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
        activation.apply(enable=["uuid-e"])

        assert activation.dependents_of(["uuid-a"]) == ["uuid-c", "uuid-b", "uuid-e"]

    def test_mod_without_dependents(self, activation: ActivationService) -> None:
        assert activation.dependents_of(["uuid-d", "uuid-missing"]) == []
