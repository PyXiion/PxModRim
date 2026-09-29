from __future__ import annotations

import re
import time
from typing import TYPE_CHECKING, Any

from pxmodrim.core.models.metadata.structures import AboutXmlMod, ListedMod
from pxmodrim.core.services.workshop_download_service import workshop_service
from pxmodrim.ui.components.unity_rich_text import unity_rich_text_to_html
from pxmodrim.ui.models.mod_list_model import provider_label

if TYPE_CHECKING:
    from pxmodrim.core.context import CoreContext
    from pxmodrim.core.models.view.diagnostics import ModIssueView

_URL_RE = re.compile(r"https?://(?:(?!&(?:quot|lt|gt|#x27|#39);)[^\s<>\"])+")
_IMG_RE = re.compile(r"\[img\]((https?)://[^\s\[<\"]+)\[/img\]", re.IGNORECASE)
_URL_TAG_RE = re.compile(
    r"\[url=(https?://[^\]\s\"<]+)\](.*?)\[/url\]", re.IGNORECASE | re.DOTALL
)
_BARE_URL_TAG_RE = re.compile(r"\[url\](https?://[^\[\s\"<]+)\[/url\]", re.IGNORECASE)
_ENTITY_RE = re.compile(r"&amp;(#\d{1,7}|#x[0-9a-fA-F]{1,6}|[a-zA-Z]{2,8});")
_QUOTE_AUTHOR_RE = re.compile(r"\[quote=([^\]]+)\]", re.IGNORECASE)
_TABLE_OPEN_RE = re.compile(r"\[table[^\]]*\]", re.IGNORECASE)
# BBCode name -> HTML tag, for tags that map one-to-one.
_PAIRED_TAGS = {
    "b": "b",
    "i": "i",
    "u": "u",
    "s": "s",
    "strike": "s",
    "h1": "h1",
    "h2": "h2",
    "h3": "h3",
    "code": "pre",
    "quote": "blockquote",
    "list": "ul",
    "olist": "ol",
    "table": "table",
    "tr": "tr",
    "td": "td",
    "th": "th",
}
_PAIRED_RE = re.compile(r"\[(/?)(" + "|".join(_PAIRED_TAGS) + r")\]", re.IGNORECASE)
# Tags that only exist to be stripped (Steam spoiler/noparse have no HTML form).
_STRIP_RE = re.compile(r"\[/?(?:spoiler|noparse)\]", re.IGNORECASE)
_BLOCK = r"(?:ul|ol|li|table|tr|td|th|blockquote|pre|h[1-3])"
_BLOCK_AFTER = r"(?:/h[1-3]|/?ul|/?ol|/?table|/?tr|/?blockquote|/?pre)"
# Line breaks the text converter emits around block tags would add blank lines.
_BR_BEFORE_BLOCK_RE = re.compile(r"(?:<br>\s*)+(?=</?" + _BLOCK + r"\b)")
_BR_AFTER_BLOCK_RE = re.compile(r"(<" + _BLOCK_AFTER + r">)(?:<br>\s*)+")
# Replaced by the QML page with the description's pixel width.
IMG_WIDTH_TOKEN = "__IMG_WIDTH__"
_TAG_RE = re.compile(r"(<[^>]+>)")
_TRAILING_PUNCT = ".,;:!?)"
_UNITS = (
    ("year", 365 * 86400),
    ("month", 30 * 86400),
    ("week", 7 * 86400),
    ("day", 86400),
    ("hour", 3600),
)


def bbcode_images(html_text: str) -> str:
    """Turn Steam-style [img]url[/img] into width-constrained <img> tags.

    Only https images are loaded; plain-http ones become a link instead.
    """

    def image(m: re.Match[str]) -> str:
        url = m.group(1)
        if m.group(2).lower() == "https":
            return f'<img src="{url}" width="{IMG_WIDTH_TOKEN}">'
        return f'<a href="{url}">{url}</a>'

    return _IMG_RE.sub(image, html_text)


def restore_entities(html_text: str) -> str:
    """Un-escape entities like &#8226; that authors wrote into plain-text fields."""
    return _ENTITY_RE.sub(r"&\1;", html_text)


def bbcode_markup(html_text: str) -> str:
    """Convert Steam-style BBCode (links, formatting, lists, tables) to HTML."""
    html_text = _URL_TAG_RE.sub(r'<a href="\1">\2</a>', html_text)
    html_text = _BARE_URL_TAG_RE.sub(r'<a href="\1">\1</a>', html_text)
    html_text = _QUOTE_AUTHOR_RE.sub(r"<blockquote><i>\1 wrote:</i><br>", html_text)
    html_text = _TABLE_OPEN_RE.sub("<table>", html_text)
    html_text = _STRIP_RE.sub("", html_text)
    html_text = re.sub(r"\[hr\]", "<hr>", html_text, flags=re.IGNORECASE)
    html_text = html_text.replace("[*]", "<li>")
    html_text = _PAIRED_RE.sub(
        lambda m: f"<{m.group(1)}{_PAIRED_TAGS[m.group(2).lower()]}>", html_text
    )
    html_text = _BR_BEFORE_BLOCK_RE.sub("", html_text)
    return _BR_AFTER_BLOCK_RE.sub(r"\1", html_text)


def autolink(html_text: str) -> str:
    """Wrap bare http(s) URLs in anchors, leaving text inside existing <a> alone."""

    def link(match: re.Match[str]) -> str:
        url = match.group(0)
        stripped = url.rstrip(_TRAILING_PUNCT)
        return f'<a href="{stripped}">{stripped}</a>{url[len(stripped) :]}'

    out = []
    in_anchor = False
    for part in _TAG_RE.split(html_text):
        if part.startswith("<"):
            lowered = part.lower()
            if lowered.startswith("<a ") or lowered == "<a>":
                in_anchor = True
            elif lowered == "</a>":
                in_anchor = False
            out.append(part)
        else:
            out.append(part if in_anchor else _URL_RE.sub(link, part))
    return "".join(out)


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
        result.append(
            {
                "name": dep.name or str(dep.package_id),
                "state": state,
                "uuid": found.uuid if found is not None else "",
            }
        )
    return result


def _conflicts(
    mod: AboutXmlMod, index: dict[str, ListedMod], active: set[str]
) -> list[str]:
    names = []
    for pid in mod.about_rules.incompatible_with:
        other = index.get(str(pid).lower())
        if other is not None and other.uuid in active:
            names.append(other.name)
    return sorted(names, key=str.lower)


def _needed_by(mod: AboutXmlMod, ctx: CoreContext) -> list[dict[str, str]]:
    pid = str(mod.package_id).lower()
    found = [
        m
        for m in ctx.all_mods.values()
        if isinstance(m, AboutXmlMod)
        and m.uuid != mod.uuid
        and any(
            pid
            in (str(d.package_id).lower(), *map(str.lower, d.alternative_package_ids))
            for d in m.about_rules.dependencies.values()
        )
    ]
    found.sort(key=lambda m: m.name.lower())
    return [{"name": m.name, "uuid": m.uuid} for m in found]


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
    needed_by: list[dict[str, str]] = []
    conflicts: list[str] = []
    if isinstance(mod, AboutXmlMod):
        index = _pid_index(ctx)
        needs = _needs(mod, index, active)
        conflicts = _conflicts(mod, index, active)
        needed_by = _needed_by(mod, ctx)

    svc = workshop_service(ctx)
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
        "conflicts": conflicts,
        "neededBy": needed_by,
        "description": autolink(
            bbcode_markup(
                bbcode_images(
                    restore_entities(unity_rich_text_to_html(mod.description))
                )
            )
        )
        if mod.description
        else "",
        "details": _details(mod),
        "canOpenFolder": mod.mod_path is not None and mod.mod_path.exists(),
        "canUpdateWorkshop": svc is not None and svc.updatable_id(mod) is not None,
        "url": mod.url if isinstance(mod, AboutXmlMod) else "",
    }
