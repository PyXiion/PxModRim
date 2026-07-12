from __future__ import annotations

import asyncio

from pxmodrim.core.context import CoreContext
from pxmodrim.models.metadata.structures import AboutXmlMod
from pxmodrim.sort.community import community_rules_path
from pxmodrim.sort.exceptions import CommunityRulesMissingError
from pxmodrim.sort.models import SortResult
from pxmodrim.sort.sorter import sort_active_mods


class SortService:
    def __init__(self, ctx: CoreContext):
        self._ctx = ctx

    async def sort_active_mods(self) -> SortResult:
        mods = self._ctx.all_mods
        active = self._ctx.active_uuids
        settings = self._ctx.config.sort

        community_path = None
        if settings.use_community_rules:
            cr_path = community_rules_path()
            if not cr_path.exists():
                raise CommunityRulesMissingError(cr_path)
            community_path = str(cr_path)

        def _sort() -> SortResult:
            return sort_active_mods(mods, active, settings, community_path)

        return await asyncio.to_thread(_sort)

    async def apply_sort(self, result: SortResult) -> bool:
        pid_to_uuid = {
            mod.package_id: uuid
            for uuid, mod in self._ctx.all_mods.items()
            if isinstance(mod, AboutXmlMod)
        }

        ordered_uuids = [pid_to_uuid[pid] for pid in result.order if pid in pid_to_uuid]

        model = self._ctx.mod_list_model
        if model:
            model.reorder(ordered_uuids)
        return True
