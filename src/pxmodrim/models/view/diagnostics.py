"""UI-agnostic view models for diagnostics data."""

from __future__ import annotations

from dataclasses import dataclass

from pxmodrim.models.metadata.structures import ListedMod


@dataclass(frozen=True)
class ModDiagnosticsView:
    has_errors: bool = False
    has_warnings: bool = False
    error_tooltip: str = ""
    warning_tooltip: str = ""


@dataclass(frozen=True)
class ModIssueView:
    category: str
    detail: str | None
    is_error: bool


@dataclass(frozen=True)
class ModItemState:
    uuid: str
    mod: ListedMod
    checked: bool
