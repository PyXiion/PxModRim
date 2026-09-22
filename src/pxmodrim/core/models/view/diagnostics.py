"""UI-agnostic view models for diagnostics data."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class ModDiagnosticsView:
    has_errors: bool = False
    has_warnings: bool = False
    error_tooltip: str = ""
    warning_tooltip: str = ""


@dataclass(frozen=True, slots=True)
class ModIssueView:
    category: str
    category_display_name: str
    detail: str | None
    is_error: bool
