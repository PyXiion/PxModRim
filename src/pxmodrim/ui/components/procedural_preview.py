from __future__ import annotations

import hashlib
from functools import lru_cache

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import (
    QColor,
    QLinearGradient,
    QPainter,
    QPainterPath,
    QPen,
    QPixmap,
)


def _seedrand(seed: str) -> float:
    h = int(hashlib.sha256(seed.encode()).hexdigest(), 16)
    return (h % 10000) / 10000


def _hsl(h: float, s: float, lum: float) -> QColor:
    return QColor.fromHslF(h % 1.0, max(0.0, min(1.0, s)), max(0.0, min(1.0, lum)))


@lru_cache(maxsize=16)
def generate_preview(title: str, width: int, height: int) -> QPixmap:
    pixmap = QPixmap(width, height)
    pixmap.fill(Qt.GlobalColor.transparent)

    base_hue = _seedrand(title)

    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)

    grad = QLinearGradient(0, 0, width, height)
    grad.setColorAt(0.0, _hsl(base_hue, 0.55, 0.30))
    grad.setColorAt(1.0, _hsl(base_hue + 0.15, 0.50, 0.22))
    painter.fillRect(0, 0, width, height, grad)

    shape_pen = QPen()
    shape_pen.setWidth(2)

    for i in range(8):
        seed = f"{title}{i}"
        x = _seedrand(seed) * width
        y = _seedrand(seed + "y") * height
        size = 20 + _seedrand(seed + "s") * 60
        shape_hue = base_hue + _seedrand(seed + "h") * 0.3 - 0.15
        alpha = 40 + int(_seedrand(seed + "a") * 50)
        color = _hsl(shape_hue, 0.5, 0.6)
        color.setAlpha(alpha)

        kind = int(_seedrand(seed + "k") * 3)
        painter.setBrush(color)
        shape_pen.setColor(color)
        painter.setPen(shape_pen)

        if kind == 0:
            painter.drawEllipse(QPointF(x, y), size / 2, size / 2)
        elif kind == 1:
            path = QPainterPath()
            path.moveTo(QPointF(x, y - size / 2))
            path.lineTo(QPointF(x + size / 2, y + size / 2))
            path.lineTo(QPointF(x - size / 2, y + size / 2))
            path.closeSubpath()
            painter.drawPath(path)
        else:
            painter.drawRect(QRectF(x - size / 2, y - size / 2, size, size))

    painter.setPen(Qt.PenStyle.NoPen)
    for i in range(30):
        seed = f"{title}dot{i}"
        x = _seedrand(seed) * width
        y = _seedrand(seed + "y") * height
        r = 1 + _seedrand(seed + "r") * 3
        alpha = 30 + int(_seedrand(seed + "a") * 40)
        dot_color = _hsl(base_hue + _seedrand(seed + "h") * 0.2, 0.4, 0.7)
        dot_color.setAlpha(alpha)
        painter.setBrush(dot_color)
        painter.drawEllipse(QPointF(x, y), r, r)

    initials = "".join(w[0] for w in title.split()[:2]).upper()
    if not initials:
        initials = title[:2].upper()

    painter.setPen(_hsl(base_hue, 0.3, 0.85))
    font = painter.font()
    font.setPointSize(max(16, height // 4))
    font.setBold(True)
    painter.setFont(font)
    painter.drawText(
        QRectF(0, 0, width, height), Qt.AlignmentFlag.AlignCenter, initials
    )

    painter.end()
    return pixmap
