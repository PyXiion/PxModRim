from __future__ import annotations

from collections.abc import Iterable
from typing import TYPE_CHECKING

from loguru import logger

from pxmodrim.core.services.workshop_download_service import workshop_service

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext


def updatable_ids(ctx: CoreContext, uuids: Iterable[str]) -> list[str]:
    """Workshop ids among the mods *uuids* that PxModRim can update."""
    svc = workshop_service(ctx)
    if svc is None:
        return []
    all_mods = ctx.all_mods
    return svc.updatable_ids(all_mods[u] for u in uuids if u in all_mods)


def can_update(ctx: CoreContext, uuids: Iterable[str]) -> bool:
    svc = workshop_service(ctx)
    if svc is None or svc.is_downloading:
        return False
    return bool(updatable_ids(ctx, uuids))


async def update_workshop_mods(ctx: CoreContext, uuids: Iterable[str]) -> None:
    """Re-sync the updatable mods among *uuids*; results are toasted by MainWindow."""
    svc = workshop_service(ctx)
    ids = updatable_ids(ctx, uuids)
    if svc is None or not ids:
        logger.debug("[workshop] update requested but nothing is updatable")
        return
    logger.info("[workshop] update requested for {} mods", len(ids))
    try:
        await svc.download_mods(ids)
    except (RuntimeError, ValueError) as exc:
        logger.warning("[workshop] update not started: {}", exc)
