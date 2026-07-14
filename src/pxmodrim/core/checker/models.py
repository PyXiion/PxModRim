from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Literal

from pxmodrim.models.metadata.structures import (
    CaseInsensitiveStr,
    ReplacementInfo,
)
from pxmodrim.sort.config import SortSettings

if TYPE_CHECKING:
    from pxmodrim.checker.graph import ConstraintGraph
    from pxmodrim.models.metadata.structures import AboutXmlMod

PackageId = CaseInsensitiveStr


@dataclass(frozen=True, slots=True)
class ModIssue:
    category: str
    severity: Literal["error", "warning"]
    short_message: str
    category_display_name: str = ""
    detail_message: str = ""
    related_package_ids: tuple[PackageId, ...] = ()


@dataclass(slots=True)
class ModDiagnostics:
    errors: list[ModIssue] = field(default_factory=list)
    warnings: list[ModIssue] = field(default_factory=list)

    @property
    def has_errors(self) -> bool:
        return bool(self.errors)

    @property
    def has_warnings(self) -> bool:
        return bool(self.warnings)

    @property
    def error_tooltip(self) -> str:
        if not self.errors:
            return ""
        if len(self.errors) == 1:
            return self.errors[0].short_message
        return f"{len(self.errors)} errors"

    @property
    def warning_tooltip(self) -> str:
        if not self.warnings:
            return ""
        if len(self.warnings) == 1:
            return self.warnings[0].short_message
        return f"{len(self.warnings)} warnings"


@dataclass(slots=True)
class CheckContext:
    active_mods: dict[PackageId, AboutXmlMod]
    ordered_pids: list[PackageId]
    pid_to_index: dict[PackageId, int]
    graph: ConstraintGraph
    settings: SortSettings
    target_version: str = "1.5"
    no_version_warning: set[PackageId] = field(default_factory=set)
    use_this_instead: Mapping[str, ReplacementInfo] = field(default_factory=dict)
    cycles: list[list[PackageId]] = field(default_factory=list)
