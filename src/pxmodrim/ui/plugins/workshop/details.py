from __future__ import annotations

import asyncio
from typing import TYPE_CHECKING

import msgspec

from pxmodrim.core.workshop import CatalogCollection, CatalogError, CatalogMod

if TYPE_CHECKING:
    from pxmodrim.core.workshop import DownloadPlan, WorkshopCatalog


class Detail(msgspec.Struct, frozen=True):
    item: CatalogMod | CatalogCollection
    members: list[CatalogMod | CatalogCollection]
    warning: str
    complete: bool


async def fetch_detail(catalog: WorkshopCatalog, item_id: str, kind: str) -> Detail:
    if kind == "collection":
        return await _collection_detail(catalog, item_id)
    return await _mod_detail(catalog, item_id)


async def _collection_detail(catalog: WorkshopCatalog, item_id: str) -> Detail:
    detail = await catalog.collection(item_id)
    if detail is None:
        raise CatalogError("This collection is no longer available.", 404)
    warning = ""
    if not detail.is_complete:
        warning = (
            "This collection is incomplete. Some members are unavailable or not listed."
        )
    if detail.unavailable_ids:
        warning += " Unavailable: " + ", ".join(detail.unavailable_ids)
    return Detail(detail.collection, list(detail.members), warning, detail.is_complete)


async def _mod_detail(catalog: WorkshopCatalog, item_id: str) -> Detail:
    mod = await catalog.mod(item_id)
    if mod is None:
        raise CatalogError("This mod is no longer available.", 404)
    dependencies = await asyncio.gather(*(catalog.mod(pid) for pid in mod.dependencies))
    missing = [
        pid
        for pid, item in zip(mod.dependencies, dependencies, strict=True)
        if item is None
    ]
    warning = "Unavailable dependencies: " + ", ".join(missing) if missing else ""
    found = [item for item in dependencies if item is not None]
    return Detail(mod, list(found), warning, not missing)


def incomplete_plan_notice(plan: DownloadPlan) -> str:
    parts = ["The download plan is incomplete."]
    if plan.unavailable_ids:
        parts.append("Unavailable: " + ", ".join(plan.unavailable_ids))
    if plan.incomplete_collection_ids:
        parts.append(
            "Incomplete collections: " + ", ".join(plan.incomplete_collection_ids)
        )
    return " ".join(parts)


def queued_notice(count: int) -> str:
    return (
        f"Queued {count} mod{'s' if count != 1 else ''}. Progress is shown "
        "in the header and results in Downloads. Mods are not activated."
    )
