from __future__ import annotations

import base64
from typing import TYPE_CHECKING, Literal

if TYPE_CHECKING:
    IconName = Literal[
        "logo",
        "refresh",
        "sort",
        "save",
        "settings",
        "search",
        "check",
        "warning",
        "error",
        "close",
        "minimize",
        "maximize",
        "restore",
        "chevron",
        "folder",
        "steam",
        "local",
        "git",
        "grid",
        "check-circle",
        "x-circle",
        "info",
        "toast-success",
        "toast-warning",
        "toast-error",
        "empty",
        "play",
        "link",
        "chevron-down",
    ]

from PySide6.QtGui import QColor, QIcon, QPainter, QPixmap
from PySide6.QtSvg import QSvgRenderer
from PySide6.QtXml import QDomDocument

# Each icon is an SVG path data string.
# stroke svg uses 24x24 viewBox, stroke-width 2, stroke="currentColor"
_ICONS: dict[str, str] = {
    "logo": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<path d="M12 2L2 7l10 5 10-5-10-5z"/>'
        '<path d="M2 17l10 5 10-5M2 12l10 5 10-5"/>'
        "</svg>"
    ),
    "refresh": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<path d="M4 4v5h5"/>'
        '<path d="M20 20v-5h-5"/>'
        '<path d="M20.49 9A9 9 0 005.64 5.64L4 7m16 10l-1.64 1.36A9 9 0 013.51 15"/>'
        "</svg>"
    ),
    "sort": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<line x1="3" y1="6" x2="21" y2="6"/>'
        '<line x1="3" y1="12" x2="15" y2="12"/>'
        '<line x1="3" y1="18" x2="9" y2="18"/>'
        "</svg>"
    ),
    "save": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<path d="M19 21H5a2 2 0 01-2-2V5a2 2 0 012-2h11l5 5v11a2 2 0 01-2 2z"/>'
        '<polyline points="17,21 17,13 7,13 7,21"/>'
        '<polyline points="7,3 7,8 15,8"/>'
        "</svg>"
    ),
    "settings": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<circle cx="12" cy="12" r="3"/>'
        '<path d="M19.4 15a1.65 1.65 0 00.33 1.82l.06.06a2 2 0 01-2.83 2.83l-.06-.06'
        "a1.65 1.65 0 00-1.82-.33 1.65 1.65 0 00-1 1.51V21a2 2 0 01-4"
        " 0v-.09A1.65 1.65 0 009 19.4a1.65 1.65 0 00-1.82.33l-.06.06a2 2 0"
        " 01-2.83-2.83l.06-.06A1.65 1.65 0 004.68 15a1.65 1.65 0"
        " 00-1.51-1H3a2 2 0 010-4h.09A1.65 1.65 0 004.6 9a1.65 1.65 0"
        " 00-.33-1.82l-.06-.06a2 2 0 012.83-2.83l.06.06A1.65 1.65 0 009 4.68"
        "a1.65 1.65 0 001-1.51V3a2 2 0 014 0v.09a1.65 1.65 0 001 1.51"
        " 1.65 1.65 0 001.82-.33l.06-.06a2 2 0 012.83 2.83l-.06.06"
        "A1.65 1.65 0 0019.4 9a1.65 1.65 0 001.51 1H21a2 2 0 010 4h-.09"
        'a1.65 1.65 0 00-1.51 1z"/>'
        "</svg>"
    ),
    "search": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<circle cx="11" cy="11" r="8"/>'
        '<line x1="21" y1="21" x2="16.65" y2="16.65"/>'
        "</svg>"
    ),
    "check": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<polyline points="20 6 9 17 4 12"/>'
        "</svg>"
    ),
    "warning": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<path d="M10.29 3.86L1.82 18a2 2 0 001.71 3h16.94a2 2 0 001.71-3'
        'L13.71 3.86a2 2 0 00-3.42 0z"/>'
        '<line x1="12" y1="9" x2="12" y2="13"/>'
        '<line x1="12" y1="17" x2="12.01" y2="17"/>'
        "</svg>"
    ),
    "error": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<circle cx="12" cy="12" r="10"/>'
        '<line x1="15" y1="9" x2="9" y2="15"/>'
        '<line x1="9" y1="9" x2="15" y2="15"/>'
        "</svg>"
    ),
    "close": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<line x1="18" y1="6" x2="6" y2="18"/>'
        '<line x1="6" y1="6" x2="18" y2="18"/>'
        "</svg>"
    ),
    "minimize": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<line x1="5" y1="12" x2="19" y2="12"/>'
        "</svg>"
    ),
    "maximize": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<rect x="5" y="5" width="14" height="14" rx="1"/>'
        "</svg>"
    ),
    "restore": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<rect x="8" y="8" width="12" height="12" rx="1"/>'
        '<path d="M4 16V4h12"/>'
        "</svg>"
    ),
    "chevron": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<polyline points="9 18 15 12 9 6"/>'
        "</svg>"
    ),
    "folder": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<path d="M22 19a2 2 0 01-2 2H4a2 2 0 01-2-2V5a2 2 0 012-2h5l2 3h9'
        'a2 2 0 012 2z"/>'
        "</svg>"
    ),
    "steam": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<circle cx="12" cy="12" r="10"/>'
        '<path d="M8 12a4 4 0 118 0 4 4 0 01-8 0"/>'
        '<circle cx="12" cy="12" r="2" fill="currentColor"/>'
        "</svg>"
    ),
    "local": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<path d="M22 19a2 2 0 01-2 2H4a2 2 0 01-2-2V5a2 2 0 012-2h5l2 3h9'
        'a2 2 0 012 2z"/>'
        '<circle cx="12" cy="13" r="2"/>'
        "</svg>"
    ),
    "git": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<path d="M9 19c-5 1.5-5-2.5-7-3m14 6v-3.87a3.37 3.37 0 00-.94-2.61'
        "c3.14-.35 6.44-1.54 6.44-7A5.44 5.44 0 0020 4.77"
        " 5.07 5.07 0 0019.91 1S18.73.65 16 2.48a13.38 13.38 0 00-7 0"
        "C6.27.65 5.09 1 5.09 1A5.07 5.07 0 005 4.77a5.44 5.44 0"
        ' 00-1.5 3.78c0 5.42 3.3 6.61 6.44 7A3.37 3.37 0 009 18.13V22"/>'
        "</svg>"
    ),
    # ── Sidebar icons ──
    "grid": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<rect x="3" y="3" width="7" height="7"/>'
        '<rect x="14" y="3" width="7" height="7"/>'
        '<rect x="3" y="14" width="7" height="7"/>'
        '<rect x="14" y="14" width="7" height="7"/>'
        "</svg>"
    ),
    "check-circle": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<polyline points="9 11 12 14 22 4"/>'
        '<path d="M21 12v7a2 2 0 01-2 2H5a2 2 0 01-2-2V5a2 2 0 012-2h11"/>'
        "</svg>"
    ),
    "x-circle": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<circle cx="12" cy="12" r="10"/>'
        '<line x1="4.93" y1="4.93" x2="19.07" y2="19.07"/>'
        "</svg>"
    ),
    "info": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<circle cx="12" cy="12" r="10"/>'
        '<line x1="12" y1="16" x2="12" y2="12"/>'
        '<line x1="12" y1="8" x2="12.01" y2="8"/>'
        "</svg>"
    ),
    "toast-success": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<path d="M22 11.08V12a10 10 0 11-5.93-9.14"/>'
        '<polyline points="22 4 12 14.01 9 11.01"/>'
        "</svg>"
    ),
    "toast-warning": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<path d="M10.29 3.86L1.82 18a2 2 0 001.71 3h16.94a2 2 0 001.71-3'
        'L13.71 3.86a2 2 0 00-3.42 0z"/>'
        '<line x1="12" y1="9" x2="12" y2="13"/>'
        '<line x1="12" y1="17" x2="12.01" y2="17"/>'
        "</svg>"
    ),
    "toast-error": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<circle cx="12" cy="12" r="10"/>'
        '<line x1="15" y1="9" x2="9" y2="15"/>'
        '<line x1="9" y1="9" x2="15" y2="15"/>'
        "</svg>"
    ),
    "play": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<polygon points="5 3 19 12 5 21 5 3"/>'
        "</svg>"
    ),
    "empty": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<path d="M13 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V9z"/>'
        '<polyline points="13 2 13 9 20 9"/>'
        "</svg>"
    ),
    "link": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<path d="M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71"/>'
        '<path d="M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71"/>'
        "</svg>"
    ),
    "chevron-down": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<polyline points="6 9 12 15 18 9"/>'
        "</svg>"
    ),
}


def _color_to_hex(color: str | QColor) -> str:
    if isinstance(color, QColor):
        return color.name()
    return color


def svg_str(name: str, color: str = "currentColor") -> str:
    raw = _ICONS[name]
    if 'xmlns="http://www.w3.org/2000/svg"' not in raw:
        raw = raw.replace("<svg ", '<svg xmlns="http://www.w3.org/2000/svg" ')
    return raw.replace("currentColor", color)


def pixmap(
    name: str,
    size: int = 16,
    color: str | QColor = "#f2f3f5",
) -> QPixmap:
    hex_color = _color_to_hex(color)
    svg = svg_str(name, hex_color)
    pm = QPixmap(size, size)
    pm.fill(QColor(0, 0, 0, 0))
    painter = QPainter(pm)
    try:
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        svg_bytes = svg.encode("utf-8")
        doc = QDomDocument()
        if doc.setContent(svg_bytes):
            renderer = QSvgRenderer(doc.toByteArray())
            renderer.render(painter)
    finally:
        painter.end()
    return pm


def icon(
    name: str,
    size: int = 16,
    color: str | QColor = "#f2f3f5",
) -> QIcon:
    pm = pixmap(name, size, color)
    return QIcon(pm)


def qml_source(name: str, color: str = "#f2f3f5") -> str:
    svg = svg_str(name, color)
    encoded = base64.b64encode(svg.encode("utf-8")).decode("utf-8")
    return f"data:image/svg+xml;base64,{encoded}"
