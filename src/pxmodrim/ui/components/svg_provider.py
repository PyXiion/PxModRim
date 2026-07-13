from __future__ import annotations

from urllib.parse import unquote

from PySide6.QtCore import QByteArray, QSize, Qt
from PySide6.QtGui import QPainter, QPixmap
from PySide6.QtQuick import QQuickImageProvider
from PySide6.QtSvg import QSvgRenderer

from pxmodrim.ui.components.icons import svg_str


class SvgIconProvider(QQuickImageProvider):
    """Serves named SVG icons to QML via image://icons/<name>?color=<hex>."""

    def __init__(self) -> None:
        super().__init__(QQuickImageProvider.Pixmap)  # pyright: ignore[reportAttributeAccessIssue]

    def requestPixmap(self, id: str, _size: QSize, requested_size: QSize) -> QPixmap:
        parts = id.split("?", 1)
        icon_name = parts[0]
        color = "#949ba4"
        if len(parts) > 1:
            for param in parts[1].split("&"):
                if param.startswith("color="):
                    raw = param[6:]
                    color = unquote(raw)
                    if not color.startswith("#") and color.startswith("23"):
                        color = "#" + color[2:]

        try:
            svg = svg_str(icon_name, color)
        except KeyError:
            svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"/>'

        renderer = QSvgRenderer(QByteArray(svg.encode()))

        sz = requested_size if requested_size.isValid() else QSize(16, 16)
        pix = QPixmap(sz)
        pix.fill(Qt.GlobalColor.transparent)
        painter = QPainter(pix)
        renderer.render(painter)
        painter.end()
        return pix
