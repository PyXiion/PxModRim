from __future__ import annotations

import base64
import random
from importlib.resources import files as resource_files

from PySide6.QtCore import Property, QObject, Signal

from pxmodrim.ui.theme import constants
from pxmodrim.ui.theme.palette import PALETTE


class Theme(QObject):
    changed = Signal()

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._logo_variants = self._load_logos()
        self._logo_data_uri = random.choice(self._logo_variants)

    @staticmethod
    def _load_logos() -> list[str]:
        uris = []
        for n in range(1, 4 + 1):
            raw = (
                resource_files("pxmodrim.ui.assets") / f"logo_nobg_{n}.svg"
            ).read_bytes()
            encoded = base64.b64encode(raw).decode("ascii")
            uris.append(f"data:image/svg+xml;base64,{encoded}")
        return uris

    # ── Elevation backgrounds ────────────────────────────

    @Property(str, notify=changed)
    def elevate0(self) -> str:
        return PALETTE["ELEVATE_0"]

    @Property(str, notify=changed)
    def elevate1(self) -> str:
        return PALETTE["ELEVATE_1"]

    @Property(str, notify=changed)
    def elevate2(self) -> str:
        return PALETTE["ELEVATE_2"]

    @Property(str, notify=changed)
    def elevate3(self) -> str:
        return PALETTE["ELEVATE_3"]

    @Property(str, notify=changed)
    def elevate4(self) -> str:
        return PALETTE["ELEVATE_4"]

    @Property(str, notify=changed)
    def border(self) -> str:
        return PALETTE["BORDER"]

    # ── Text ─────────────────────────────────────────────

    @Property(str, notify=changed)
    def textMain(self) -> str:
        return PALETTE["TEXT_MAIN"]

    @Property(str, notify=changed)
    def textMuted(self) -> str:
        return PALETTE["TEXT_MUTED"]

    @Property(str, notify=changed)
    def textDim(self) -> str:
        return PALETTE["TEXT_DIM"]

    @Property(str, notify=changed)
    def onAccent(self) -> str:
        return PALETTE["TEXT_ON_ACCENT"]

    @Property(str, notify=changed)
    def neutral(self) -> str:
        return PALETTE["NEUTRAL"]

    # ── Accent / Semantic (*Bg are #AARRGGBB, valid QML colors) ──

    @Property(str, notify=changed)
    def primary(self) -> str:
        return PALETTE["PRIMARY"]

    @Property(str, notify=changed)
    def primaryHover(self) -> str:
        return PALETTE["PRIMARY_HOVER"]

    @Property(str, notify=changed)
    def primaryBg(self) -> str:
        return PALETTE["PRIMARY_BG_QML"]

    @Property(str, notify=changed)
    def success(self) -> str:
        return PALETTE["SUCCESS"]

    @Property(str, notify=changed)
    def successHover(self) -> str:
        return PALETTE["SUCCESS_HOVER"]

    @Property(str, notify=changed)
    def successBg(self) -> str:
        return PALETTE["SUCCESS_BG_QML"]

    @Property(str, notify=changed)
    def warning(self) -> str:
        return PALETTE["WARNING"]

    @Property(str, notify=changed)
    def warningBg(self) -> str:
        return PALETTE["WARNING_BG_QML"]

    @Property(str, notify=changed)
    def danger(self) -> str:
        return PALETTE["DANGER"]

    @Property(str, notify=changed)
    def dangerHover(self) -> str:
        return PALETTE["DANGER_HOVER"]

    @Property(str, notify=changed)
    def dangerBg(self) -> str:
        return PALETTE["DANGER_BG_QML"]

    # ── Design tokens ────────────────────────────────────

    @Property(str, constant=True)
    def fontFamily(self) -> str:
        return "Source Sans 3, Liberation Sans, sans-serif"

    @Property(str, constant=True)
    def fontMono(self) -> str:
        return "DejaVu Sans Mono, Liberation Mono, monospace"

    @Property(int, constant=True)
    def fontSizeXs(self) -> int:
        return 10

    @Property(int, constant=True)
    def fontSizeSm(self) -> int:
        return 11

    @Property(int, constant=True)
    def fontSizeMd(self) -> int:
        return 13

    @Property(int, constant=True)
    def fontSizeLg(self) -> int:
        return 14

    @Property(int, constant=True)
    def fontSizeXl(self) -> int:
        return 18

    @Property(int, constant=True)
    def radiusXs(self) -> int:
        return 3

    @Property(int, constant=True)
    def radiusSm(self) -> int:
        return 4

    @Property(int, constant=True)
    def radiusMd(self) -> int:
        return 6

    @Property(int, constant=True)
    def radiusLg(self) -> int:
        return 8

    @Property(int, constant=True)
    def radiusPill(self) -> int:
        return 999

    @Property(int, constant=True)
    def scrollbarWidth(self) -> int:
        return 6

    @Property(int, constant=True)
    def tooltipDelay(self) -> int:
        return constants.TOOLTIP_DELAY_MS

    @Property(float, constant=True)
    def disabledOpacity(self) -> float:
        return 0.45

    @Property(str, constant=True)
    def overlay(self) -> str:
        return "#8c000000"

    @Property(int, constant=True)
    def railMinWidth(self) -> int:
        return constants.RAIL_MIN_WIDTH

    @Property(int, constant=True)
    def railMaxWidth(self) -> int:
        return constants.RAIL_MAX_WIDTH

    @Property(int, constant=True)
    def railCollapseWidth(self) -> int:
        return constants.RAIL_COLLAPSE_WIDTH

    @Property(int, constant=True)
    def sidebarWidth(self) -> int:
        return constants.SIDEBAR_WIDTH

    @Property(list, constant=True)
    def logoVariants(self) -> list[str]:
        return self._logo_variants

    @Property(str, constant=True)
    def logoFileDataUri(self) -> str:
        return self._logo_data_uri
