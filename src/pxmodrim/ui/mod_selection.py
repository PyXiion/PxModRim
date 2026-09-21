from __future__ import annotations

from pxmodrim.core.context import CoreContext
from pxmodrim.core.models.metadata.structures import AboutXmlMod
from pxmodrim.ui.panels.mod_info_panel import ModInfoPanel


class ModSelectionPresenter:
    """Loads a selected mod's details into a ModInfoPanel for any view."""

    def __init__(self, ctx: CoreContext, panel: ModInfoPanel) -> None:
        self._ctx = ctx
        self._panel = panel
        self._generation = 0

    async def show(self, uuid: str) -> None:
        generation = self._generation + 1
        self._generation = generation

        if not uuid:
            self._panel.clear()
            return

        mod = self._ctx.all_mods.get(uuid)
        if mod is None:
            self._panel.clear()
            return

        self._panel.show_mod(mod)
        self._panel.set_issues(self._ctx.diagnostics_service.issues_for(uuid))
        pid = getattr(mod, "package_id", None)
        active_pids = self._resolve_active_pids()

        if generation != self._generation:
            return
        await self._panel.set_time_analytics(
            str(pid) if pid is not None else None,
            active_pids,
        )

    def clear(self) -> None:
        self._generation += 1
        self._panel.clear()

    def _resolve_active_pids(self) -> list[str]:
        active = set(self._ctx.active_uuids)
        return [
            str(m.package_id).lower()
            for m in self._ctx.all_mods.values()
            if isinstance(m, AboutXmlMod) and m.uuid in active
        ]
