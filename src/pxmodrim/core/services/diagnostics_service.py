from __future__ import annotations

import operator
from pathlib import Path
from typing import TYPE_CHECKING

from ttimer import Timer

from pxmodrim.core.checker.checker import ModChecker
from pxmodrim.core.checker.databases import (
    NoVersionWarningService,
    UseThisInsteadService,
)
from pxmodrim.core.checker.issues import (
    CycleIssueChecker,
    DependencyIssueChecker,
    GameVersionIssueChecker,
    IncompatibilityIssueChecker,
    LoadOrderIssueChecker,
    ReplacementIssueChecker,
)
from pxmodrim.core.checker.models import ModDiagnostics
from pxmodrim.core.events import Event
from pxmodrim.core.models.view.diagnostics import (
    ModDiagnosticsView,
    ModIssueView,
)
from pxmodrim.core.models.view.sidebar import (
    PROVIDER_LABELS,
    ActiveModsEntry,
    AllModsEntry,
    ErrorModsEntry,
    InactiveModsEntry,
    ProviderModsEntry,
    SidebarEntry,
    WarningModsEntry,
)
from pxmodrim.core.sort.models import CommunityRule, PackageId

if TYPE_CHECKING:
    from pxmodrim.core.checker.graph import ConstraintGraph
    from pxmodrim.core.config import ConfigService
    from pxmodrim.core.context import CoreContext
    from pxmodrim.core.models.metadata.structures import AboutXmlMod, ListedMod


class DiagnosticsService:
    diagnostics_summary_changed: Event[dict[str, ModDiagnosticsView]]
    status_message_changed: Event[str]
    sidebar_entries_changed: Event[list[SidebarEntry]]

    __slots__ = (
        "_checker",
        "_community_rules",
        "_ctx",
        "_last_active_uuids",
        "_last_summary",
        "_last_summary_sources",
        "_no_version_warning_service",
        "_sidebar_base",
        "_use_this_instead_service",
        "diagnostics_summary_changed",
        "sidebar_entries_changed",
        "status_message_changed",
    )

    def __init__(
        self, ctx: CoreContext, config_service: ConfigService | None = None
    ) -> None:
        """Initialise diagnostics with checkers, databases, and community rules."""
        self.diagnostics_summary_changed = Event()
        self.status_message_changed = Event()
        self.sidebar_entries_changed = Event()
        self._ctx = ctx
        cs = config_service or ctx.config_service
        self._no_version_warning_service = NoVersionWarningService(cs)
        self._use_this_instead_service = UseThisInsteadService(cs)
        self._community_rules: dict[PackageId, CommunityRule] | None = None
        self._last_active_uuids: list[str] = []
        self._last_summary: dict[str, ModDiagnosticsView] = {}
        # Diagnostics each summary view was built from; reorders keep most
        # diagnostics objects, so their views can be reused.
        self._last_summary_sources: dict[str, ModDiagnostics] = {}
        self._sidebar_base: _SidebarBase | None = None
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

    async def initialize(self, timer: Timer | None = None) -> None:
        """Load databases and community rules, then apply them to the checker."""
        if timer is None:
            await self._ensure_databases()
            self._community_rules = await self._load_community_rules()
            if self._community_rules:
                self._checker.set_community_rules(self._community_rules)
            return
        with timer("diagnostics.init"):
            with timer("ensure_databases"):
                await self._ensure_databases()
            with timer("load_community_rules"):
                self._community_rules = await self._load_community_rules()
            if self._community_rules:
                self._checker.set_community_rules(self._community_rules)

    async def _ensure_databases(self) -> None:
        """Fetch or load NoVersionWarning and UseThisInstead databases."""
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
        """Load community sorting rules from disk, if enabled."""
        if not self._ctx.config.sort.use_community_rules:
            return None
        from pxmodrim.core.sort.community import load_community_rules

        path = self._ctx.config.paths.community_rules_file
        if not path:
            path = str(self._ctx.config_service.config_dir / "communityRules.json")
        if not path or not Path(path).exists():
            return None
        return load_community_rules(Path(path))

    # ── Mutations ──────────────────────────────────────────────

    def rebuild(
        self, active_uuids: list[str] | None = None, timer: Timer | None = None
    ) -> None:
        """Rebuild the checker for all mods with the given active UUIDs."""
        if active_uuids is None:
            active_uuids = self._ctx.active_uuids
        self._last_active_uuids = active_uuids
        if timer is None:
            self._checker.rebuild(self._ctx.all_mods, active_uuids)
            return
        with timer("diagnostics.rebuild"):
            self._checker.rebuild(self._ctx.all_mods, active_uuids, timer=timer)

    def reorder(self, active_uuids: list[str]) -> None:
        """Notify the checker of a new active mod order."""
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
        """Return view models for all diagnostics associated with a given mod UUID."""
        diag = self._checker.diagnostics_for(uuid)
        return self._to_issue_views(diag) if diag else []

    # ── Conversions ────────────────────────────────────────────

    @staticmethod
    def _to_issue_views(diag: ModDiagnostics) -> list[ModIssueView]:
        """Convert a ModDiagnostics object into a list of ModIssueView models."""
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
    def _to_view(diag: ModDiagnostics) -> ModDiagnosticsView:
        """Convert diagnostics to a serialisable view for the UI."""
        return ModDiagnosticsView(
            has_errors=diag.has_errors,
            has_warnings=diag.has_warnings,
            error_tooltip=diag.error_tooltip,
            warning_tooltip=diag.warning_tooltip,
        )

    # ── Checker callback ───────────────────────────────────────

    def _on_checker_diagnostics_changed(
        self, diagnostics: dict[str, ModDiagnostics]
    ) -> None:
        """Callback from checker; emit updated summary, status, and sidebar signals."""
        previous_views = self._last_summary
        previous_sources = self._last_summary_sources
        summary: dict[str, ModDiagnosticsView] = {}
        for uuid, diag in diagnostics.items():
            if previous_sources.get(uuid) is diag:
                summary[uuid] = previous_views[uuid]
            else:
                summary[uuid] = self._to_view(diag)
        self._last_summary = summary
        self._last_summary_sources = diagnostics
        self.diagnostics_summary_changed.emit(self._last_summary)
        self.status_message_changed.emit(self._format_status())
        self.sidebar_entries_changed.emit(self._build_sidebar_entries())

    def summary_for(self, uuid: str) -> ModDiagnosticsView | None:
        return self._last_summary.get(uuid)

    # ── Status ──────────────────────────────────────────────────

    def _format_status(self) -> str:
        """Build a human-readable status string showing active/error/warning counts."""
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
        """Build sidebar entries categorised by provider, active, error, warning."""
        mods = self._ctx.all_mods
        active = self._last_active_uuids
        diagnostics = self._checker.active_mod_diagnostics()

        base = self._sidebar_base
        if base is None or not base.matches(mods):
            base = self._sidebar_base = _SidebarBase(mods)

        errors = set(base.invalid)
        warnings: set[str] = set()
        for u, diag in diagnostics.items():
            if u not in mods:
                continue
            if diag.has_errors:
                errors.add(u)
            if diag.has_warnings:
                warnings.add(u)

        entries: list[SidebarEntry] = [
            AllModsEntry(),
            ActiveModsEntry(),
        ]
        for pid, uuids in base.by_provider:
            entries.append(
                ProviderModsEntry(pid, PROVIDER_LABELS.get(pid, pid), set(uuids))
            )
        entries.append(InactiveModsEntry())
        entries.append(ErrorModsEntry())
        entries.append(WarningModsEntry())

        entries[0].visible_uuids = set(base.all_uuids)
        entries[0].refresh_count()
        entries[1].visible_uuids = set(active)
        entries[1].refresh_count()
        entries[-3].visible_uuids = entries[0].visible_uuids - entries[1].visible_uuids
        entries[-3].refresh_count()
        entries[-2].visible_uuids = errors
        entries[-2].refresh_count()
        entries[-1].visible_uuids = warnings
        entries[-1].refresh_count()

        return entries


class _SidebarBase:
    """Diagnostics-independent sidebar partitions, reused until the mods change."""

    __slots__ = ("all_uuids", "by_provider", "invalid", "mods")

    def __init__(self, mods: dict[str, ListedMod]) -> None:
        self.mods = mods
        by_provider: dict[str, set[str]] = {}
        invalid: set[str] = set()
        for u, m in mods.items():
            by_provider.setdefault(m.provider_id, set()).add(u)
            if not m.valid:
                invalid.add(u)
        self.by_provider = sorted(by_provider.items(), key=lambda item: item[0])
        self.invalid = frozenset(invalid)
        self.all_uuids = frozenset(mods)

    def matches(self, mods: dict[str, ListedMod]) -> bool:
        # Callers pass copies of one dict, so identical iteration order is the
        # common case; any difference just forces a (correct) recompute.
        cached = self.mods
        return (
            len(cached) == len(mods)
            and all(map(operator.is_, cached.values(), mods.values()))
            and all(map(operator.eq, cached, mods))
        )
