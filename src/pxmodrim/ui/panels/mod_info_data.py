from __future__ import annotations

import time
from typing import TYPE_CHECKING, Any

from pxmodrim.core.models.metadata.structures import AboutXmlMod, ListedMod
from pxmodrim.ui.components.unity_rich_text import unity_rich_text_to_html
from pxmodrim.ui.models.mod_list_model import provider_label

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext
    from pxmodrim.core.models.view.diagnostics import ModIssueView

_NEEDED_BY_LIMIT = 6
_UNITS = (
    ("year", 365 * 86400),
    ("month", 30 * 86400),
    ("week", 7 * 86400),
    ("day", 86400),
    ("hour", 3600),
)


def format_age(mtime: float, now: float | None = None) -> str:
    if mtime <= 0:
        return ""
    delta = (time.time() if now is None else now) - mtime
    for name, seconds in _UNITS:
        if delta >= seconds:
            count = int(delta // seconds)
            return f"{count} {name}{'s' if count != 1 else ''} ago"
    return "just now"


def _issue_dicts(issues: list[ModIssueView]) -> list[dict[str, Any]]:
    return [
        {
            "title": i.category_display_name,
            "detail": i.detail or "",
            "isError": i.is_error,
        }
        for i in issues
    ]


def _status_title(issues: list[ModIssueView]) -> tuple[str, str]:
    errors = sum(1 for i in issues if i.is_error)
    warnings = len(issues) - errors
    if errors:
        return "error", f"{errors} error{'s' if errors != 1 else ''}"
    if warnings:
        return "warning", f"{warnings} warning{'s' if warnings != 1 else ''}"
    return "ok", "No issues"


def _pid_index(ctx: CoreContext) -> dict[str, ListedMod]:
    index: dict[str, ListedMod] = {}
    for m in ctx.all_mods.values():
        if isinstance(m, AboutXmlMod):
            index.setdefault(str(m.package_id).lower(), m)
    return index


def _needs(
    mod: AboutXmlMod, index: dict[str, ListedMod], active: set[str]
) -> list[dict[str, str]]:
    result = []
    for dep in mod.about_rules.dependencies.values():
        candidates = [str(dep.package_id), *map(str, dep.alternative_package_ids)]
        found = next((index[c.lower()] for c in candidates if c.lower() in index), None)
        if found is None:
            state = "missing"
        elif found.uuid in active:
            state = "active"
        else:
            state = "inactive"
        result.append({"name": dep.name or str(dep.package_id), "state": state})
    return result


def _needed_by(mod: AboutXmlMod, ctx: CoreContext) -> tuple[list[str], int]:
    pid = str(mod.package_id).lower()
    names = [
        m.name
        for m in ctx.all_mods.values()
        if isinstance(m, AboutXmlMod)
        and m.uuid != mod.uuid
        and any(
            pid
            in (str(d.package_id).lower(), *map(str.lower, d.alternative_package_ids))
            for d in m.about_rules.dependencies.values()
        )
    ]
    names.sort(key=str.lower)
    return names[:_NEEDED_BY_LIMIT], max(0, len(names) - _NEEDED_BY_LIMIT)


def _version_support(mod: ListedMod, target: str) -> dict[str, Any]:
    if not mod.supported_versions or target == "Unknown":
        return {"known": False, "ok": False, "target": target}
    return {"known": True, "ok": target in mod.supported_versions, "target": target}


def _details(mod: ListedMod) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    if isinstance(mod, AboutXmlMod):
        if mod.authors:
            rows.append({"label": "Author", "value": ", ".join(mod.authors)})
        if mod.mod_version:
            rows.append({"label": "Version", "value": mod.mod_version})
        source = provider_label(mod.provider_id, str(mod.package_id))
        if source:
            rows.append({"label": "Source", "value": source})
        if mod.mtime > 0:
            rows.append({"label": "Updated", "value": format_age(mod.mtime)})
        rows.append({"label": "Package ID", "value": str(mod.package_id), "copy": True})
    if mod.mod_path is not None:
        rows.append({"label": "Path", "value": str(mod.mod_path), "copy": True})
    return rows


def build_mod_info(
    ctx: CoreContext, mod: ListedMod, issues: list[ModIssueView]
) -> dict[str, Any]:
    active_list = ctx.active_uuids
    active = set(active_list)
    is_active = mod.uuid in active
    level, title = _status_title(issues)
    is_about = isinstance(mod, AboutXmlMod)

    needs: list[dict[str, str]] = []
    needed_by: list[str] = []
    needed_by_more = 0
    if isinstance(mod, AboutXmlMod):
        needs = _needs(mod, _pid_index(ctx), active)
        needed_by, needed_by_more = _needed_by(mod, ctx)

    return {
        "name": mod.name,
        "author": ", ".join(mod.authors) if isinstance(mod, AboutXmlMod) else "",
        "provider": (
            provider_label(mod.provider_id, str(mod.package_id)) if is_about else ""
        ),
        "status": {
            "level": level,
            "title": title,
            "active": is_active,
            "position": active_list.index(mod.uuid) + 1 if is_active else 0,
            "total": len(active_list),
            "issues": _issue_dicts(issues),
            "obsolete": mod.obsolete,
            "valid": mod.valid,
        },
        "version": _version_support(mod, ctx.target_version),
        "needs": needs,
        "neededBy": needed_by,
        "neededByMore": needed_by_more,
        "description": unity_rich_text_to_html(mod.description)
        if mod.description
        else "",
        "details": _details(mod),
        "canOpenFolder": mod.mod_path is not None and mod.mod_path.exists(),
        "url": mod.url if isinstance(mod, AboutXmlMod) else "",
    }
