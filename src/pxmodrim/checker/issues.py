from __future__ import annotations

from abc import ABC, abstractmethod
from typing import TYPE_CHECKING

from pxmodrim.checker.graph import EdgeType
from pxmodrim.checker.models import CheckContext, ModIssue, PackageId
from pxmodrim.models.metadata.structures import AboutXmlMod

if TYPE_CHECKING:
    pass


class ModIssueChecker(ABC):
    def should_check(self, mod: AboutXmlMod, ctx: CheckContext) -> bool:
        return True

    @abstractmethod
    def check(self, mod: AboutXmlMod, ctx: CheckContext) -> list[ModIssue]: ...


class DependencyIssueChecker(ModIssueChecker):
    def should_check(self, mod: AboutXmlMod, ctx: CheckContext) -> bool:
        return bool(mod.overall_rules.dependencies)

    def check(self, mod: AboutXmlMod, ctx: CheckContext) -> list[ModIssue]:
        if not ctx.settings.check_missing_dependencies:
            return []

        issues: list[ModIssue] = []
        consider_alternatives = ctx.settings.use_alternative_package_ids

        for dep_id, dep_mod in mod.overall_rules.dependencies.items():
            satisfied = str(dep_id) in ctx.active_mods
            if not satisfied and consider_alternatives:
                satisfied = any(
                    str(alt) in ctx.active_mods
                    for alt in dep_mod.alternative_package_ids
                )

            if not satisfied:
                alt_note = ""
                if consider_alternatives:
                    alt_candidates = [str(a) for a in dep_mod.alternative_package_ids]
                    if alt_candidates:
                        alt_note = f" (alternatives: {', '.join(alt_candidates)})"

                issues.append(
                    ModIssue(
                        category="missing_dependency",
                        severity="error",
                        short_message="Missing dependencies",
                        detail_message=(
                            f"Dependency not satisfied: {dep_id}{alt_note}"
                        ),
                        related_package_ids=(PackageId(dep_id),),
                    )
                )

        return issues


class IncompatibilityIssueChecker(ModIssueChecker):
    def should_check(self, mod: AboutXmlMod, ctx: CheckContext) -> bool:
        pid = PackageId(mod.package_id)
        outgoing = ctx.graph.edges_of_type(pid, EdgeType.INCOMPATIBILITY)
        incoming = ctx.graph.incoming_of_type(pid, EdgeType.INCOMPATIBILITY)
        return (
            bool(mod.overall_rules.incompatible_with)
            or bool(outgoing)
            or bool(incoming)
        )

    def check(self, mod: AboutXmlMod, ctx: CheckContext) -> list[ModIssue]:
        issues: list[ModIssue] = []
        pid = PackageId(mod.package_id)

        # Self-declared incompatibilities
        for incomp in mod.overall_rules.incompatible_with:
            if str(incomp) in ctx.active_mods:
                other = ctx.active_mods.get(PackageId(incomp))
                name = other.name if other else str(incomp)
                issues.append(
                    ModIssue(
                        category="incompatibility",
                        severity="error",
                        short_message="Incompatibilities detected",
                        detail_message=f"Mod declares incompatibility with: {name} ({incomp})",
                        related_package_ids=(PackageId(incomp),),
                    )
                )

        # Reverse incompatibilities (other mods declaring this mod incompatible)
        self_declared = set(mod.about_rules.incompatible_with)
        for edge in ctx.graph.incoming_of_type(pid, EdgeType.INCOMPATIBILITY):
            if edge.source in ctx.active_mods and edge.source != pid:
                # Check if this is already counted (mutual declaration)
                if edge.source in self_declared:
                    continue
                other = ctx.active_mods.get(edge.source)
                name = other.name if other else str(edge.source)
                # Only add if this edge originates from community rules or from
                # about_xml that we already didn't count (non-mutual)
                source_mod = ctx.active_mods.get(edge.source)
                if (
                    source_mod
                    and PackageId(str(pid))
                    not in source_mod.about_rules.incompatible_with
                ):
                    issues.append(
                        ModIssue(
                            category="incompatibility",
                            severity="error",
                            short_message="Incompatibilities detected",
                            detail_message=f"Declared incompatible by: {name} ({edge.source})",
                            related_package_ids=(PackageId(edge.source),),
                        )
                    )

        return issues


class LoadOrderIssueChecker(ModIssueChecker):
    def should_check(self, mod: AboutXmlMod, ctx: CheckContext) -> bool:
        pid = PackageId(mod.package_id)
        return bool(
            ctx.graph.edges_of_type(pid, EdgeType.LOAD_BEFORE)
            or ctx.graph.edges_of_type(pid, EdgeType.LOAD_AFTER)
        )

    def check(self, mod: AboutXmlMod, ctx: CheckContext) -> list[ModIssue]:
        issues: list[ModIssue] = []
        pid = PackageId(mod.package_id)
        idx = ctx.pid_to_index.get(pid, -1)
        if idx < 0:
            return issues

        for edge in ctx.graph.edges_of_type(pid, EdgeType.LOAD_BEFORE):
            other_idx = ctx.pid_to_index.get(edge.target, -1)
            if other_idx >= 0 and idx >= other_idx:
                other = ctx.active_mods.get(edge.target)
                name = other.name if other else str(edge.target)
                issues.append(
                    ModIssue(
                        category="load_order",
                        severity="warning",
                        short_message="Load order violated",
                        detail_message=(
                            f"Should load before: {name} ({edge.target}), "
                            f"but appears after (index {idx} vs {other_idx})"
                        ),
                        related_package_ids=(PackageId(edge.target),),
                    )
                )

        for edge in ctx.graph.edges_of_type(pid, EdgeType.LOAD_AFTER):
            other_idx = ctx.pid_to_index.get(edge.target, -1)
            if other_idx >= 0 and idx <= other_idx:
                other = ctx.active_mods.get(edge.target)
                name = other.name if other else str(edge.target)
                issues.append(
                    ModIssue(
                        category="load_order",
                        severity="warning",
                        short_message="Load order violated",
                        detail_message=(
                            f"Should load after: {name} ({edge.target}), "
                            f"but appears before (index {idx} vs {other_idx})"
                        ),
                        related_package_ids=(PackageId(edge.target),),
                    )
                )

        return issues


class CycleIssueChecker(ModIssueChecker):
    def check(self, mod: AboutXmlMod, ctx: CheckContext) -> list[ModIssue]:
        pid = PackageId(mod.package_id)
        issues: list[ModIssue] = []

        for cycle in ctx.cycles:
            if pid in cycle:
                issues.append(
                    ModIssue(
                        category="cycle",
                        severity="warning",
                        short_message="Dependency cycle detected",
                        detail_message=(
                            "Part of a dependency cycle: "
                            + " -> ".join(str(c) for c in cycle)
                        ),
                        related_package_ids=tuple(c for c in cycle if c != pid),
                    )
                )

        return issues


class GameVersionIssueChecker(ModIssueChecker):
    def should_check(self, mod: AboutXmlMod, ctx: CheckContext) -> bool:
        return bool(mod.supported_versions)

    def check(self, mod: AboutXmlMod, ctx: CheckContext) -> list[ModIssue]:
        if not mod.supported_versions:
            return []

        pid = str(mod.package_id).lower()
        if pid in ctx.no_version_warning:
            return []

        target = ctx.target_version.split(".")[:2]
        target_major_minor = ".".join(target)

        if target_major_minor not in mod.supported_versions:
            return [
                ModIssue(
                    category="version_mismatch",
                    severity="warning",
                    short_message="Version mismatch",
                    detail_message=(
                        f"Mod supports {', '.join(sorted(mod.supported_versions))}, "
                        f"but game is on {target_major_minor}"
                    ),
                )
            ]

        return []


class ReplacementIssueChecker(ModIssueChecker):
    def check(self, mod: AboutXmlMod, ctx: CheckContext) -> list[ModIssue]:
        pid = str(mod.published_file_id) if mod.published_file_id else None
        if pid is None:
            return []

        replacement = ctx.use_this_instead.get(pid) if ctx.use_this_instead else None
        if replacement is None:
            return []

        return [
            ModIssue(
                category="replacement_available",
                severity="warning",
                short_message="Replacement available",
                detail_message=(
                    f"Recommended replacement: {replacement.name} "
                    f"({replacement.packageid}) by {replacement.author}"
                ),
                related_package_ids=(PackageId(replacement.packageid),),
            )
        ]
