from __future__ import annotations

from PySide6.QtCore import QSize, Qt
from PySide6.QtGui import (
    QColor,
    QLinearGradient,
    QPainter,
    QPaintEvent,
    QPixmap,
    QResizeEvent,
)
from PySide6.QtWidgets import QWidget

from pxmodrim.ui.theme.palette import PANEL_BG_Q, TEXT_MAIN_Q, TEXT_MUTED_Q


class AspectRatioBanner(QWidget):
    """Banner that scales pixmap to width, preserves aspect ratio, clamps max height."""

    def __init__(self, parent: QWidget | None = None, max_height: int = 260) -> None:
        super().__init__(parent)
        self._pixmap: QPixmap | None = None
        self._scaled_pixmap: QPixmap | None = None
        self._title = ""
        self._subtitle = ""
        self._max_height = max_height
        self._overlay_height = 120
        self._show_overlay = True
        self.setSizePolicy(
            self.sizePolicy().Policy.Expanding,
            self.sizePolicy().Policy.Fixed,
        )

    def setPixmap(self, pixmap: QPixmap) -> None:
        self._pixmap = pixmap
        self._recompute_scaled_pixmap()
        self.updateGeometry()
        self.update()

    def _recompute_scaled_pixmap(self) -> None:
        if not self._pixmap or self._pixmap.isNull() or self.width() <= 0:
            self._scaled_pixmap = None
            return

        target_w = self.width()
        ratio = self._pixmap.height() / self._pixmap.width()
        target_h = int(target_w * ratio)
        target_h = min(target_h, self._max_height)

        self._scaled_pixmap = self._pixmap.scaled(
            target_w,
            target_h,
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation,
        )

    def setTitle(self, title: str) -> None:
        self._title = title
        self.update()

    def setSubtitle(self, subtitle: str) -> None:
        self._subtitle = subtitle or ""
        self.update()

    def setShowOverlay(self, show: bool) -> None:
        self._show_overlay = show
        self.update()

    def sizeHint(self) -> QSize:
        return QSize(self.width() or 300, self._max_height)

    def paintEvent(self, event: QPaintEvent) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        painter.setClipRect(self.rect())

        # Background fill when no pixmap
        if not self._scaled_pixmap or self._scaled_pixmap.isNull():
            painter.fillRect(self.rect(), PANEL_BG_Q)
        else:
            y = (self.height() - self._scaled_pixmap.height()) // 2
            painter.drawPixmap(0, y, self._scaled_pixmap)

        # Gradient overlay at bottom (only when no actual preview image)
        if self._title and self._show_overlay:
            gradient = QLinearGradient(
                0, self.height() - self._overlay_height, 0, self.height()
            )
            gradient.setColorAt(0.0, QColor(0, 0, 0, 0))
            gradient.setColorAt(1.0, QColor(0, 0, 0, 180))

            painter.fillRect(
                0,
                self.height() - self._overlay_height,
                self.width(),
                self._overlay_height,
                gradient,
            )

            pad = 16
            text_area_top = self.height() - self._overlay_height + 12
            text_area_w = self.width() - pad * 2

            font = painter.font()
            font.setPointSize(18)
            font.setBold(True)
            painter.setFont(font)
            painter.setPen(TEXT_MAIN_Q)

            title_h = self._overlay_height - 24
            if self._subtitle:
                title_h = title_h * 3 // 5
            painter.drawText(
                pad,
                text_area_top,
                text_area_w,
                title_h,
                Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignBottom,
                self._title,
            )

            if self._subtitle:
                font.setPointSize(12)
                font.setBold(False)
                painter.setFont(font)
                painter.setPen(TEXT_MUTED_Q)
                subtitle_top = text_area_top + title_h + 2
                subtitle_h = self._overlay_height - title_h - 26
                painter.drawText(
                    pad,
                    subtitle_top,
                    text_area_w,
                    subtitle_h,
                    Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop,
                    self._subtitle,
                )

    def resizeEvent(self, event: QResizeEvent) -> None:
        self._recompute_scaled_pixmap()
        super().resizeEvent(event)
