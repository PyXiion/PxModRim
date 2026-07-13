from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING

from PySide6.QtCore import QObject, Signal

from pxmodrim.checker.checker import ModChecker
from pxmodrim.checker.databases import NoVersionWarningService, UseThisInsteadService
from pxmodrim.checker.issues import (
    CycleIssueChecker,
    DependencyIssueChecker,
    GameVersionIssueChecker,
    IncompatibilityIssueChecker,
    LoadOrderIssueChecker,
    ReplacementIssueChecker,
)
from pxmodrim.checker.models import ModDiagnostics
from pxmodrim.models.view.diagnostics import (
    ModDiagnosticsView,
    ModIssueView,
    ModItemState,
)
from pxmodrim.models.view.sidebar import (
    PROVIDER_LABELS,
    ActiveModsEntry,
    AllModsEntry,
    ErrorModsEntry,
    InactiveModsEntry,
    ProviderModsEntry,
    SidebarEntry,
    WarningModsEntry,
)
from pxmodrim.sort.models import CommunityRule, PackageId

if TYPE_CHECKING:
    from pxmodrim.checker.graph import ConstraintGraph
    from pxmodrim.core.context import CoreContext
    from pxmodrim.models.metadata.structures import AboutXmlMod


class DiagnosticsService(QObject):
    diagnostics_summary_changed = Signal(dict)  # dict[str, ModDiagnosticsView]
    status_message_changed = Signal(str)
    sidebar_entries_changed = Signal(list)  # list[SidebarEntry]

    def __init__(self, ctx: CoreContext) -> None:
        super().__init__()
        self._ctx = ctx
        self._no_version_warning_service = NoVersionWarningService()
        self._use_this_instead_service = UseThisInsteadService()
        self._community_rules: dict[PackageId, CommunityRule] | None = None
        self._last_active_uuids: list[str] = []
        self._checker = ModChecker(
            checkers=[
                DependencyIssueChecker(),
                IncompatibilityIssueChecker(),
                LoadOrderIssueChecker(),
                CycleIssueChecker(),
                GameVersionIssueChecker(),
                ReplacementIssueChecker(),
            ],
            settings=ctx.config.sort,
            target_version=ctx.target_version,
            on_diagnostics_changed=self._on_checker_diagnostics_changed,
        )

    # ── Lifecycle ──────────────────────────────────────────────

    async def initialize(self) -> None:
        await self._ensure_databases()
        self._community_rules = await self._load_community_rules()
        if self._community_rules:
            self._checker.set_community_rules(self._community_rules)

    async def _ensure_databases(self) -> None:
        nvw = await self._no_version_warning_service.ensure()
        if nvw:
            self._checker.set_no_version_warning(nvw)
        else:
            nvw = self._no_version_warning_service.load_if_exists()
            if nvw:
                self._checker.set_no_version_warning(nvw)

        uti = await self._use_this_instead_service.ensure()
        if uti:
            self._checker.set_use_this_instead(uti)
        else:
            uti = self._use_this_instead_service.load_if_exists()
            if uti:
                self._checker.set_use_this_instead(uti)

    async def _load_community_rules(self) -> dict[PackageId, CommunityRule] | None:
        if not self._ctx.config.sort.use_community_rules:
            return None
        from pxmodrim.sort.community import community_rules_path, load_community_rules

        path = self._ctx.config.paths.community_rules_file
        if not path:
            path = str(community_rules_path())
        if not path or not Path(path).exists():
            return None
        return load_community_rules(Path(path))

    # ── Mutations ──────────────────────────────────────────────

    def rebuild(self, active_uuids: list[str] | None = None) -> None:
        if active_uuids is None:
            active_uuids = self._ctx.active_uuids
        self._last_active_uuids = active_uuids
        self._checker.rebuild(self._ctx.all_mods, active_uuids)

    def apply_active_mods_change(self, states: list[ModItemState]) -> None:
        active_uuids = [s.uuid for s in states if s.checked]
        self._last_active_uuids = active_uuids
        self._checker.rebuild(self._ctx.all_mods, active_uuids)

    def reorder(self, active_uuids: list[str]) -> None:
        self._last_active_uuids = active_uuids
        self._checker.reorder(active_uuids)

    # ── Queries ────────────────────────────────────────────────

    @property
    def active_mods_by_pid(self) -> dict[PackageId, AboutXmlMod]:
        return self._checker.active_mods

    @property
    def constraint_graph(self) -> ConstraintGraph:
        return self._checker.graph

    @property
    def active_ordered_pids(self) -> list[PackageId]:
        return self._checker.ordered_pids

    @property
    def community_rules(self) -> dict[PackageId, CommunityRule] | None:
        return self._checker.community_rules

    def issues_for(self, uuid: str) -> list[ModIssueView]:
        diag = self._checker.diagnostics_for(uuid)
        return self._to_issue_views(diag) if diag else []

    # ── Conversions ────────────────────────────────────────────

    @staticmethod
    def _to_issue_views(diag: ModDiagnostics) -> list[ModIssueView]:
        views: list[ModIssueView] = []
        for issue in diag.errors:
            views.append(
                ModIssueView(
                    category=issue.category,
                    category_display_name=issue.category_display_name or issue.category,
                    detail=issue.detail_message or None,
                    is_error=True,
                )
            )
        for issue in diag.warnings:
            views.append(
                ModIssueView(
                    category=issue.category,
                    category_display_name=issue.category_display_name or issue.category,
                    detail=issue.detail_message or None,
                    is_error=False,
                )
            )
        return views

    @staticmethod
    def _to_view(
        diagnostics: dict[str, ModDiagnostics],
    ) -> dict[str, ModDiagnosticsView]:
        result: dict[str, ModDiagnosticsView] = {}
        for uuid, diag in diagnostics.items():
            result[uuid] = ModDiagnosticsView(
                has_errors=diag.has_errors,
                has_warnings=diag.has_warnings,
                error_tooltip=diag.error_tooltip,
                warning_tooltip=diag.warning_tooltip,
            )
        return result

    # ── Checker callback ───────────────────────────────────────

    def _on_checker_diagnostics_changed(
        self, diagnostics: dict[str, ModDiagnostics]
    ) -> None:
        self.diagnostics_summary_changed.emit(self._to_view(diagnostics))
        self.status_message_changed.emit(self._format_status())
        self.sidebar_entries_changed.emit(self._build_sidebar_entries())

    # ── Status ──────────────────────────────────────────────────

    def _format_status(self) -> str:
        active = self._last_active_uuids
        diagnostics = self._checker.active_mod_diagnostics()

        err_count = sum(1 for d in diagnostics.values() if d.has_errors)
        warn_count = sum(1 for d in diagnostics.values() if d.has_warnings)

        parts: list[str] = [f"{len(active)} active"]
        if err_count:
            parts.append(f"{err_count} with errors")
        if warn_count:
            parts.append(f"{warn_count} with warnings")
        return " | ".join(parts)

    # ── Sidebar ─────────────────────────────────────────────────

    def _build_sidebar_entries(self) -> list[SidebarEntry]:
        mods = self._ctx.all_mods
        active = self._last_active_uuids
        diagnostics = self._checker.active_mod_diagnostics()

        by_provider: dict[str, list[str]] = {}
        errors: list[str] = []
        warnings: list[str] = []
        for u, m in mods.items():
            by_provider.setdefault(m.provider_id, []).append(u)
            if not m.valid:
                errors.append(u)
            diag = diagnostics.get(u)
            if diag:
                if diag.has_errors:
                    errors.append(u)
                if diag.has_warnings:
                    warnings.append(u)

        entries: list[SidebarEntry] = [
            AllModsEntry(),
            ActiveModsEntry(),
        ]
        all_uuids = set(mods.keys())
        for pid in sorted(by_provider):
            entries.append(
                ProviderModsEntry(
                    pid,
                    PROVIDER_LABELS.get(pid, pid),
                    set(by_provider[pid]),
                )
            )
        entries.append(InactiveModsEntry())
        entries.append(ErrorModsEntry())
        entries.append(WarningModsEntry())

        entries[0].visible_uuids = all_uuids
        entries[0].refresh_count()
        entries[1].visible_uuids = set(active)
        entries[1].refresh_count()
        entries[-3].visible_uuids = all_uuids - set(active)
        entries[-3].refresh_count()
        entries[-2].visible_uuids = set(errors)
        entries[-2].refresh_count()
        entries[-1].visible_uuids = set(warnings)
        entries[-1].refresh_count()

        return entries
