from __future__ import annotations

from collections.abc import Callable, Mapping
from typing import TYPE_CHECKING, Any

from loguru import logger

from pxmodrim.core.checker.graph import ConstraintGraph
from pxmodrim.core.checker.models import (
    CheckContext,
    ModDiagnostics,
    ModIssue,
    PackageId,
)
from pxmodrim.core.models.metadata.structures import AboutXmlMod, ListedMod

if TYPE_CHECKING:
    from ttimer import Timer

    from pxmodrim.core.checker.issues import ModIssueChecker
    from pxmodrim.core.sort.community import CommunityRule
    from pxmodrim.core.sort.config import SortSettings


class ModChecker:
    """Orchestrates diagnostic checks across active mods."""

    __slots__ = (
        "_active_mods",
        "_all_mods",
        "_cached_cycles",
        "_checker_results",
        "_checkers",
        "_community_rules",
        "_diagnostics",
        "_graph",
        "_no_version_warning",
        "_on_diagnostics_changed",
        "_ordered_pids",
        "_settings",
        "_target_version",
        "_use_this_instead",
        "_uuid_to_pid",
    )

    def __init__(
        self,
        checkers: list[ModIssueChecker],
        settings: SortSettings,
        target_version: str = "1.5",
        on_diagnostics_changed: Callable[[dict[str, ModDiagnostics]], Any]
        | None = None,
    ) -> None:
        self._checkers = checkers
        self._settings = settings
        self._target_version = target_version
        self._on_diagnostics_changed = on_diagnostics_changed

        self._graph = ConstraintGraph()
        self._all_mods: dict[str, ListedMod] = {}
        self._diagnostics: dict[PackageId, ModDiagnostics] = {}
        self._active_mods: dict[PackageId, AboutXmlMod] = {}
        self._uuid_to_pid: dict[str, PackageId] = {}
        self._ordered_pids: list[PackageId] = []

        self._no_version_warning: set[PackageId] = set()
        self._use_this_instead: Mapping[str, Any] = {}
        self._community_rules: dict[PackageId, CommunityRule] | None = None
        self._cached_cycles: list[list[PackageId]] = []
        # Per-mod issues from each checker (checker order), reused by reorder()
        # for order-independent checkers; None when stale.
        self._checker_results: dict[PackageId, list[list[ModIssue]]] | None = None

    # ── Database config ───────────────────────────────────────

    def set_no_version_warning(self, pids: set[PackageId]) -> None:
        self._no_version_warning = pids
        self._checker_results = None

    def set_use_this_instead(self, db: Mapping[str, Any]) -> None:
        self._use_this_instead = db
        self._checker_results = None

    def set_community_rules(self, rules: dict[PackageId, CommunityRule] | None) -> None:
        self._community_rules = rules

    # ── Rebuild ───────────────────────────────────────────────

    def rebuild(
        self,
        mods: dict[str, ListedMod],
        ordered_uuids: list[str],
        timer: Timer | None = None,
    ) -> None:
        """Re-scan all mods and regenerate diagnostics from scratch."""
        from ttimer import Timer

        t = timer or Timer()

        with t("collect_active"):
            self._collect_active(mods, ordered_uuids)

        with t("graph.build"):
            self._graph.build(
                self._active_mods,
                self._ordered_pids,
                self._settings,
                self._community_rules,
            )

        with t("find_cycles"):
            self._cached_cycles = self._graph.find_cycles()
        ctx = self._build_context(self._cached_cycles)

        with t("check_mod"):
            self._check_all(ctx)

        self._emit()

    # ── Incremental updates ───────────────────────────────────

    def toggle_mod(
        self,
        mod: ListedMod,
        active: bool,
        ordered_uuids: list[str],
    ) -> None:
        """Rebuild constraints and diagnostics after toggling a mod."""
        if not isinstance(mod, AboutXmlMod):
            self._emit()
            return

        self.rebuild(self._all_mods, ordered_uuids)

    def move_mod(self, uuid: str, old_index: int, new_index: int) -> None:
        """Move a mod to new position and recheck diagnostics for neighbors."""
        if not self._ordered_pids:
            return

        pid = self._uuid_to_pid.get(uuid)
        if pid is None:
            return

        self._ordered_pids.insert(new_index, self._ordered_pids.pop(old_index))
        self._graph.update_order(self._ordered_pids)

        ctx = self._build_context(self._cached_cycles)
        # Only neighbours are rechecked, so cached results no longer match the order.
        self._checker_results = None

        affected = {pid}
        affected.update(self._graph.neighbors(pid))
        for current_pid in affected:
            if current_pid in self._active_mods:
                current_mod = self._active_mods[current_pid]
                diag = self._check_mod(current_mod, ctx)
                self._diagnostics[current_pid] = diag

        self._emit()

    def reorder(self, ordered_uuids: list[str]) -> None:
        """Reapply the full active-mod order and regenerate all diagnostics."""
        previous = self._active_mods
        previous_index = self._graph.pid_to_index
        self._collect_active(self._all_mods, ordered_uuids)
        self._graph.update_order(self._ordered_pids)

        cycles = self._graph.find_cycles()
        ctx = self._build_context(cycles)

        results = self._checker_results
        if results is None or not _same_mods(previous, self._active_mods):
            self._check_all(ctx)
        else:
            moved = {
                pid
                for pid, index in ctx.pid_to_index.items()
                if previous_index.get(pid) != index
            }
            changed: set[PackageId] = set()
            for i, checker in enumerate(self._checkers):
                if not checker.order_dependent:
                    continue
                affected = checker.affected_by_moves(ctx, moved)
                for pid, mod in self._active_mods.items():
                    if affected is not None and pid not in affected:
                        continue
                    issues = self._run_checker(checker, mod, ctx)
                    if issues != results[pid][i]:
                        results[pid][i] = issues
                        changed.add(pid)
            for pid in changed:
                self._diagnostics[pid] = _compose(results[pid])

        self._emit()

    # ── Query ─────────────────────────────────────────────────

    @property
    def active_mods(self) -> dict[PackageId, AboutXmlMod]:
        return dict(self._active_mods)

    @property
    def graph(self) -> ConstraintGraph:
        return self._graph

    @property
    def ordered_pids(self) -> list[PackageId]:
        return list(self._ordered_pids)

    @property
    def community_rules(self) -> dict[PackageId, CommunityRule] | None:
        return self._community_rules

    def diagnostics_for(self, uuid: str) -> ModDiagnostics | None:
        """Return diagnostics for a given UUID, or None if not active."""
        pid = self._uuid_to_pid.get(uuid)
        if pid is None:
            return None
        return self._diagnostics.get(pid)

    def active_mod_diagnostics(self) -> dict[str, ModDiagnostics]:
        """Return diagnostics for all active mods, keyed by UUID."""
        result: dict[str, ModDiagnostics] = {}
        for uuid, pid in self._uuid_to_pid.items():
            diag = self._diagnostics.get(pid)
            if diag:
                result[uuid] = diag
        return result

    # ── Private ───────────────────────────────────────────────

    def _collect_active(
        self, mods: dict[str, ListedMod], ordered_uuids: list[str]
    ) -> None:
        """Populate internal active-mod state from full mod list and UUID ordering."""
        self._all_mods = dict(mods)
        self._active_mods = {}
        self._uuid_to_pid = {}
        self._ordered_pids = []

        for uuid in ordered_uuids:
            mod = mods.get(uuid)
            if isinstance(mod, AboutXmlMod):
                pid = mod.package_id
                if type(pid) is not PackageId:
                    pid = PackageId(pid)
                self._active_mods[pid] = mod
                self._uuid_to_pid[uuid] = pid
                self._ordered_pids.append(pid)

    def _build_context(
        self,
        cycles: list[list[PackageId]],
    ) -> CheckContext:
        return CheckContext(
            active_mods=self._active_mods,
            ordered_pids=self._ordered_pids,
            pid_to_index=self._graph.pid_to_index,
            graph=self._graph,
            settings=self._settings,
            target_version=self._target_version,
            no_version_warning=self._no_version_warning,
            use_this_instead=self._use_this_instead,
            cycles=cycles,
        )

    def _check_all(self, ctx: CheckContext) -> None:
        """Run every checker on every active mod, caching per-checker results."""
        results: dict[PackageId, list[list[ModIssue]]] = {}
        diagnostics: dict[PackageId, ModDiagnostics] = {}
        checkers = self._checkers
        run = self._run_checker
        for pid, mod in self._active_mods.items():
            per_checker = [run(c, mod, ctx) for c in checkers]
            results[pid] = per_checker
            diagnostics[pid] = _compose(per_checker)
        self._checker_results = results
        self._diagnostics = diagnostics

    def _check_mod(self, mod: AboutXmlMod, ctx: CheckContext) -> ModDiagnostics:
        """Run all registered checkers against a single mod and collect diagnostics."""
        return _compose([self._run_checker(c, mod, ctx) for c in self._checkers])

    @staticmethod
    def _run_checker(
        checker: ModIssueChecker, mod: AboutXmlMod, ctx: CheckContext
    ) -> list[ModIssue]:
        if not checker.should_check(mod, ctx):
            return []
        try:
            issues = checker.check(mod, ctx)
        except Exception:  # noqa: BLE001 - one checker must not abort diagnostics
            logger.exception(
                f"Checker {type(checker).__name__} failed on {mod.package_id}"
            )
            return []
        return issues if type(issues) is list else list(issues)

    def _emit(self) -> None:
        if self._on_diagnostics_changed is not None:
            uuid_diag: dict[str, ModDiagnostics] = {}
            for uuid, pid in self._uuid_to_pid.items():
                diag = self._diagnostics.get(pid)
                if diag and (diag.has_errors or diag.has_warnings):
                    uuid_diag[uuid] = diag
            self._on_diagnostics_changed(uuid_diag)


def _compose(per_checker: list[list[ModIssue]]) -> ModDiagnostics:
    issues = [issue for checker_issues in per_checker for issue in checker_issues]
    if not issues:
        return ModDiagnostics([], [])
    return ModDiagnostics(
        [issue for issue in issues if issue.severity == "error"],
        [issue for issue in issues if issue.severity != "error"],
    )


def _same_mods(
    a: dict[PackageId, AboutXmlMod], b: dict[PackageId, AboutXmlMod]
) -> bool:
    return a.keys() == b.keys() and all(b[pid] is mod for pid, mod in a.items())
