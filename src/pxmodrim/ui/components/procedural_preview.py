from __future__ import annotations

import hashlib
from functools import lru_cache

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QPainter, QPixmap, QRadialGradient

from pxmodrim.ui.theme.constants import BANNER_OVERLAY_HEIGHT


def _seedrand(seed: str) -> float:
    h = int(hashlib.sha256(seed.encode()).hexdigest(), 16)
    return (h % 10000) / 10000


def _hsl(h: float, s: float, lum: float) -> QColor:
    return QColor.fromHslF(h % 1.0, max(0.0, min(1.0, s)), max(0.0, min(1.0, lum)))


def _blob(
    painter: QPainter, cx: float, cy: float, radius: float, color: QColor
) -> None:
    # Radial falloff stands in for a gaussian blur, which QPainter lacks.
    grad = QRadialGradient(QPointF(cx, cy), radius)
    inner = QColor(color)
    outer = QColor(color)
    outer.setAlpha(0)
    grad.setColorAt(0.0, inner)
    grad.setColorAt(1.0, outer)
    painter.setBrush(grad)
    painter.drawEllipse(QPointF(cx, cy), radius, radius)


@lru_cache(maxsize=16)
def generate_preview(title: str, width: int, height: int) -> QPixmap:
    """Aurora: dark tinted base, two soft hue-shifted glows, ghosted initials."""
    pixmap = QPixmap(width, height)
    hue = _seedrand(title)
    pixmap.fill(_hsl(hue, 0.40, 0.12))

    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.setPen(Qt.PenStyle.NoPen)

    span = max(width, height)
    jitter_x = _seedrand(title + "jx") * 0.2
    jitter_y = _seedrand(title + "jy") * 0.2
    warm = _hsl(hue, 0.70, 0.50)
    warm.setAlphaF(0.75)
    cool = _hsl(hue + 0.14, 0.70, 0.50)
    cool.setAlphaF(0.65)
    _blob(
        painter, width * (0.15 + jitter_x), height * (0.1 + jitter_y), span * 0.6, warm
    )
    _blob(
        painter, width * (0.85 - jitter_x), height * (0.9 - jitter_y), span * 0.55, cool
    )

    initials = "".join(w[0] for w in title.split()[:2]).upper() or title[:2].upper()
    font = painter.font()
    font.setBold(True)
    font.setWeight(font.Weight.ExtraBold)
    font.setPixelSize(max(32, int(height * 0.36)))
    font.setLetterSpacing(font.SpacingType.PercentageSpacing, 96)
    painter.setFont(font)
    painter.setPen(QColor(255, 255, 255, 210))
    visible = max(height - BANNER_OVERLAY_HEIGHT, height // 2)
    painter.drawText(
        QRectF(0, 0, width, visible), Qt.AlignmentFlag.AlignCenter, initials
    )

    painter.end()
    return pixmap
