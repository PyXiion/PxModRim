from __future__ import annotations

from typing import TYPE_CHECKING, Literal

if TYPE_CHECKING:
    IconName = Literal[
        "logo",
        "download",
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
        "chevron-left",
        "chevron-down",
        "folder",
        "tag",
        "steam",
        "local",
        "git",
        "grid",
        "home",
        "mods",
        "check-circle",
        "ban",
        "info",
        "empty",
        "play",
        "link",
        "clock",
        "trash",
        "donut",
        "bars",
        "grip",
    ]

from PySide6.QtCore import QRectF
from PySide6.QtGui import QColor, QGuiApplication, QIcon, QPainter, QPixmap
from PySide6.QtSvg import QSvgRenderer
from PySide6.QtXml import QDomDocument

from pxmodrim.ui.theme.palette import PALETTE

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
    "download": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4"/>'
        '<polyline points="7,10 12,15 17,10"/>'
        '<line x1="12" y1="15" x2="12" y2="3"/>'
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
    "chevron-left": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<polyline points="15 18 9 12 15 6"/>'
        "</svg>"
    ),
    "folder": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<path d="M22 19a2 2 0 01-2 2H4a2 2 0 01-2-2V5a2 2 0 012-2h5l2 3h9'
        'a2 2 0 012 2z"/>'
        "</svg>"
    ),
    "tag": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<path d="M20.5 13.5 13.5 20.5a2 2 0 0 1-2.8 0l-8.2-8.2V3h9.3l8.7 7.7'
        'a2 2 0 0 1 0 2.8z"/>'
        '<circle cx="7.5" cy="7.5" r="1"/>'
        "</svg>"
    ),
    "steam": (
        '<svg viewBox="0 0 24 24" fill="currentColor">'
        '<path d="M11.979 0C5.678 0 .511 4.86.022 11.037l6.432 2.658'
        "c.545-.371 1.203-.59 1.912-.59.063 0 .125.004.188.006l2.861-4.142V8.91"
        "c0-2.495 2.028-4.524 4.524-4.524 2.494 0 4.524 2.031 4.524 4.527"
        "s-2.03 4.525-4.524 4.525h-.105l-4.076 2.911c0 .052.004.105.004.159"
        " 0 1.875-1.515 3.396-3.39 3.396-1.635 0-3.016-1.173-3.331-2.727"
        "L.436 15.27C1.862 20.307 6.486 24 11.979 24c6.627 0 11.999-5.373"
        " 11.999-12S18.605 0 11.979 0zM7.54 18.21l-1.473-.61"
        "c.262.543.714.999 1.314 1.25 1.297.539 2.793-.076 3.332-1.375"
        ".263-.63.264-1.319.005-1.949s-.75-1.121-1.377-1.383"
        "c-.624-.26-1.29-.249-1.878-.03l1.523.63c.956.4 1.409 1.5 1.009 2.455"
        "-.397.957-1.497 1.41-2.454 1.012H7.54zm11.415-9.303"
        "c0-1.662-1.353-3.015-3.015-3.015-1.665 0-3.015 1.353-3.015 3.015"
        " 0 1.665 1.35 3.015 3.015 3.015 1.663 0 3.015-1.35 3.015-3.015z"
        "m-5.273-.005c0-1.252 1.013-2.266 2.265-2.266 1.249 0 2.266 1.014"
        " 2.266 2.266 0 1.251-1.017 2.265-2.266 2.265-1.253 0-2.265-1.014"
        '-2.265-2.265z"/>'
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
    "home": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>'
        '<polyline points="9 22 9 12 15 12 15 22"/>'
        "</svg>"
    ),
    "mods": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<rect x="3" y="4" width="18" height="4" rx="1"/>'
        '<rect x="3" y="10" width="18" height="4" rx="1"/>'
        '<rect x="3" y="16" width="18" height="4" rx="1"/>'
        '<line x1="7" y1="6" x2="7" y2="6"/>'
        '<line x1="7" y1="12" x2="7" y2="12"/>'
        '<line x1="7" y1="18" x2="7" y2="18"/>'
        "</svg>"
    ),
    "check-circle": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<circle cx="12" cy="12" r="10"/>'
        '<polyline points="8 12 11 15 16 9"/>'
        "</svg>"
    ),
    "ban": (
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
    "copy": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<rect x="9" y="9" width="13" height="13" rx="2"/>'
        '<path d="M5 15H4a2 2 0 01-2-2V4a2 2 0 012-2h9a2 2 0 012 2v1"/>'
        "</svg>"
    ),
    "chevron-down": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<polyline points="6 9 12 15 18 9"/>'
        "</svg>"
    ),
    "clock": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<circle cx="12" cy="12" r="10"/>'
        '<polyline points="12 6 12 12 16 14"/>'
        "</svg>"
    ),
    "trash": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<polyline points="3 6 5 6 21 6"/>'
        '<path d="M19 6v14a2 2 0 01-2 2H7a2 2 0 01-2-2V6m3 0V4a2 2 0 012-2h4a2 2 0'
        ' 012 2v2"/>'
        '<line x1="10" y1="11" x2="10" y2="17"/>'
        '<line x1="14" y1="11" x2="14" y2="17"/>'
        "</svg>"
    ),
    "donut": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<path d="M21.21 15.89A10 10 0 1 1 8 2.83"/>'
        '<path d="M22 12A10 10 0 0 0 12 2v10z"/>'
        "</svg>"
    ),
    "bars": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<line x1="18" y1="20" x2="18" y2="10"/>'
        '<line x1="12" y1="20" x2="12" y2="4"/>'
        '<line x1="6" y1="20" x2="6" y2="14"/>'
        "</svg>"
    ),
    "grip": (
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3"'
        ' stroke-linecap="round" stroke-linejoin="round">'
        '<line x1="12" y1="5" x2="12" y2="5"/>'
        '<line x1="12" y1="12" x2="12" y2="12"/>'
        '<line x1="12" y1="19" x2="12" y2="19"/>'
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
    color: str | QColor = PALETTE["TEXT_MUTED"],
) -> QPixmap:
    hex_color = _color_to_hex(color)
    svg = svg_str(name, hex_color)
    # Rasterise at >=2x so small icons stay crisp even on 1x screens.
    screen = QGuiApplication.primaryScreen()
    scale = max(2.0, screen.devicePixelRatio() if screen else 1.0)
    pm = QPixmap(round(size * scale), round(size * scale))
    pm.setDevicePixelRatio(scale)
    pm.fill(QColor(0, 0, 0, 0))
    painter = QPainter(pm)
    try:
        painter.setRenderHints(
            QPainter.RenderHint.Antialiasing | QPainter.RenderHint.SmoothPixmapTransform
        )
        svg_bytes = svg.encode("utf-8")
        doc = QDomDocument()
        if doc.setContent(svg_bytes):
            renderer = QSvgRenderer(doc.toByteArray())
            renderer.render(painter, QRectF(0, 0, size, size))
    finally:
        painter.end()
    return pm


def icon(
    name: str,
    size: int = 16,
    color: str | QColor = PALETTE["TEXT_MUTED"],
) -> QIcon:
    pm = pixmap(name, size, color)
    return QIcon(pm)
