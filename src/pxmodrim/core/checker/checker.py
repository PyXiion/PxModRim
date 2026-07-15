from __future__ import annotations

from collections.abc import Callable, Mapping
from typing import TYPE_CHECKING, Any

from loguru import logger

from pxmodrim.core.checker.graph import ConstraintGraph
from pxmodrim.core.checker.models import (
    CheckContext,
    ModDiagnostics,
    PackageId,
)
from pxmodrim.core.models.metadata.structures import AboutXmlMod, ListedMod

if TYPE_CHECKING:
    from pxmodrim.core.checker.issues import ModIssueChecker
    from pxmodrim.core.sort.community import CommunityRule
    from pxmodrim.core.sort.config import SortSettings
    from ttimer import Timer


class ModChecker:
    """Orchestrates diagnostic checks across active mods."""

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

    # ── Database config ───────────────────────────────────────

    def set_no_version_warning(self, pids: set[PackageId]) -> None:
        self._no_version_warning = pids

    def set_use_this_instead(self, db: Mapping[str, Any]) -> None:
        self._use_this_instead = db

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

        self._diagnostics = {}
        for pid, mod in self._active_mods.items():
            with t("check_mod"):
                diag = self._check_mod(mod, ctx)
            self._diagnostics[pid] = diag

        self._emit()

    # ── Incremental updates ───────────────────────────────────

    def toggle_mod(
        self,
        mod: ListedMod,
        active: bool,
        ordered_uuids: list[str],
    ) -> None:
        """Add/remove a mod from active set, update diagnostics for affected mods."""
        if not isinstance(mod, AboutXmlMod):
            self._emit()
            return

        pid = PackageId(mod.package_id)
        self._collect_active(
            dict(self._all_mods),
            ordered_uuids,
        )

        if active:
            idx = self._ordered_pids.index(pid) if pid in self._ordered_pids else -1
            if idx >= 0:
                self._graph.add_mod(
                    pid, mod, idx, self._settings, self._community_rules
                )
        else:
            self._graph.remove_mod(pid)

        self._cached_cycles = self._graph.find_cycles()
        ctx = self._build_context(self._cached_cycles)

        affected = {pid}
        affected.update(self._graph.neighbors(pid))
        for current_pid in affected:
            if current_pid in self._active_mods:
                current_mod = self._active_mods[current_pid]
                diag = self._check_mod(current_mod, ctx)
                self._diagnostics[current_pid] = diag
            else:
                self._diagnostics.pop(current_pid, None)

        self._emit()

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
        self._collect_active(
            dict(self._all_mods),
            ordered_uuids,
        )
        self._graph.update_order(self._ordered_pids)

        cycles = self._graph.find_cycles()
        ctx = self._build_context(cycles)

        self._diagnostics = {}
        for pid, mod in self._active_mods.items():
            diag = self._check_mod(mod, ctx)
            self._diagnostics[pid] = diag

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
                pid = PackageId(mod.package_id)
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

    def _check_mod(self, mod: AboutXmlMod, ctx: CheckContext) -> ModDiagnostics:
        """Run all registered checkers against a single mod and collect diagnostics."""
        errors: list[Any] = []
        warnings: list[Any] = []

        for checker in self._checkers:
            if not checker.should_check(mod, ctx):
                continue
            try:
                issues = checker.check(mod, ctx)
            except Exception:
                logger.exception(
                    f"Checker {type(checker).__name__} failed on {mod.package_id}"
                )
                continue
            for issue in issues:
                if issue.severity == "error":
                    errors.append(issue)
                else:
                    warnings.append(issue)

        return ModDiagnostics(errors=errors, warnings=warnings)

    def _emit(self) -> None:
        if self._on_diagnostics_changed is not None:
            uuid_diag: dict[str, ModDiagnostics] = {}
            for uuid, pid in self._uuid_to_pid.items():
                diag = self._diagnostics.get(pid)
                if diag and (diag.has_errors or diag.has_warnings):
                    uuid_diag[uuid] = diag
            self._on_diagnostics_changed(uuid_diag)
