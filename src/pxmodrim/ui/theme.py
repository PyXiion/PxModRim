from __future__ import annotations

from PySide6.QtCore import Property, QObject, Signal

from pxmodrim.ui.components.icons import svg_str
from pxmodrim.ui.palette import PALETTE


class Theme(QObject):
    changed = Signal()

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)

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
    def surface(self) -> str:
        return PALETTE["SURFACE"]

    # ── Legacy aliases (for existing QML) ────────────────

    @Property(str, notify=changed)
    def mainBg(self) -> str:
        return PALETTE["ELEVATE_1"]

    @Property(str, notify=changed)
    def panelBg(self) -> str:
        return PALETTE["ELEVATE_2"]

    @Property(str, notify=changed)
    def itemHover(self) -> str:
        return PALETTE["ELEVATE_3"]

    @Property(str, notify=changed)
    def itemActive(self) -> str:
        return PALETTE["ELEVATE_4"]

    @Property(str, notify=changed)
    def border(self) -> str:
        return PALETTE["BORDER"]

    @Property(str, notify=changed)
    def buttonHover(self) -> str:
        return PALETTE["ELEVATE_4"]

    @Property(str, notify=changed)
    def searchBg(self) -> str:
        return PALETTE["ELEVATE_1"]

    @Property(str, notify=changed)
    def inputBg(self) -> str:
        return PALETTE["ELEVATE_0"]

    @Property(str, notify=changed)
    def tagBg(self) -> str:
        return PALETTE["ELEVATE_3"]

    @Property(str, notify=changed)
    def depBg(self) -> str:
        return PALETTE["ELEVATE_3"]

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
    def descText(self) -> str:
        return PALETTE["TEXT_MUTED"]

    # ── Accent / Semantic ────────────────────────────────

    @Property(str, notify=changed)
    def primary(self) -> str:
        return PALETTE["PRIMARY"]

    @Property(str, notify=changed)
    def primaryHover(self) -> str:
        return PALETTE["PRIMARY_HOVER"]

    @Property(str, notify=changed)
    def primaryBg(self) -> str:
        return PALETTE["PRIMARY_BG"]

    @Property(str, notify=changed)
    def success(self) -> str:
        return PALETTE["SUCCESS"]

    @Property(str, notify=changed)
    def successBg(self) -> str:
        return PALETTE["SUCCESS_BG"]

    @Property(str, notify=changed)
    def warning(self) -> str:
        return PALETTE["WARNING"]

    @Property(str, notify=changed)
    def warningBg(self) -> str:
        return PALETTE["WARNING_BG"]

    @Property(str, notify=changed)
    def danger(self) -> str:
        return PALETTE["DANGER"]

    @Property(str, notify=changed)
    def dangerBg(self) -> str:
        return PALETTE["DANGER_BG"]

    # ── Legacy accent aliases ────────────────────────────

    @Property(str, notify=changed)
    def blue(self) -> str:
        return PALETTE["PRIMARY"]

    @Property(str, notify=changed)
    def blueHover(self) -> str:
        return PALETTE["PRIMARY_HOVER"]

    @Property(str, notify=changed)
    def green(self) -> str:
        return PALETTE["SUCCESS"]

    # ── Design tokens ────────────────────────────────────

    @Property(str, constant=True)
    def fontFamily(self) -> str:
        return "Source Sans 3, Liberation Sans, sans-serif"

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
    def radiusSm(self) -> int:
        return 4

    @Property(int, constant=True)
    def radiusMd(self) -> int:
        return 6

    @Property(int, constant=True)
    def radiusLg(self) -> int:
        return 8

    @Property(int, constant=True)
    def scrollbarWidth(self) -> int:
        return 6

    @Property(int, constant=True)
    def delegateHeight(self) -> int:
        return 52

    @Property(int, constant=True)
    def spacingXs(self) -> int:
        return 4

    @Property(int, constant=True)
    def spacingSm(self) -> int:
        return 8

    @Property(int, constant=True)
    def spacingMd(self) -> int:
        return 12

    @Property(int, constant=True)
    def spacingLg(self) -> int:
        return 16

    @Property(int, constant=True)
    def spacingXl(self) -> int:
        return 24

    # ── SVG icon sources (for QML) ─────────────────────────

    @Property(str, constant=True)
    def logoSvg(self) -> str:
        return svg_str("logo", "currentColor")

    @Property(str, constant=True)
    def refreshSvg(self) -> str:
        return svg_str("refresh", "currentColor")

    @Property(str, constant=True)
    def sortSvg(self) -> str:
        return svg_str("sort", "currentColor")

    @Property(str, constant=True)
    def saveSvg(self) -> str:
        return svg_str("save", "currentColor")

    @Property(str, constant=True)
    def settingsSvg(self) -> str:
        return svg_str("settings", "currentColor")

    # ── Sidebar / Mod list SVG icons ──────────────────────

    @Property(str, constant=True)
    def gridSvg(self) -> str:
        return svg_str("grid", "currentColor")

    @Property(str, constant=True)
    def checkCircleSvg(self) -> str:
        return svg_str("check-circle", "currentColor")

    @Property(str, constant=True)
    def xCircleSvg(self) -> str:
        return svg_str("x-circle", "currentColor")

    @Property(str, constant=True)
    def errorSvg(self) -> str:
        return svg_str("error", "currentColor")

    @Property(str, constant=True)
    def warningSvg(self) -> str:
        return svg_str("warning", "currentColor")

    @Property(str, constant=True)
    def steamSvg(self) -> str:
        return svg_str("steam", "currentColor")

    @Property(str, constant=True)
    def folderSvg(self) -> str:
        return svg_str("folder", "currentColor")
