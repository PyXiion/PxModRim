from __future__ import annotations

from PySide6.QtCore import QSize, Qt
from PySide6.QtGui import QLinearGradient, QPainter, QPaintEvent, QPixmap, QResizeEvent, QColor
from PySide6.QtWidgets import QWidget


class AspectRatioBanner(QWidget):
    """Banner that scales pixmap to width, preserves aspect ratio, clamps max height."""
    
    def __init__(self, max_height: int = 300, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._pixmap: QPixmap | None = None
        self._title = ""
        self._max_height = max_height
        self._overlay_height = 80
        self.setMinimumHeight(60)
        self.setSizePolicy(
            self.sizePolicy().Policy.Expanding,
            self.sizePolicy().Policy.Fixed,
        )
    
    def setPixmap(self, pixmap: QPixmap) -> None:
        self._pixmap = pixmap
        self.updateGeometry()
        self.update()
    
    def setTitle(self, title: str) -> None:
        self._title = title
        self.update()
    
    def sizeHint(self) -> QSize:
        if self._pixmap and not self._pixmap.isNull():
            w = self.width() or self.minimumWidth()
            h = int(w * self._pixmap.height() / self._pixmap.width())
            return QSize(w, min(h, self._max_height))
        return QSize(100, 120)
    
    def paintEvent(self, event: QPaintEvent) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        
        if self._pixmap and not self._pixmap.isNull():
            target_w = self.width()
            target_h = int(target_w * self._pixmap.height() / self._pixmap.width())
            target_h = min(target_h, self._max_height)
            
            scaled = self._pixmap.scaled(
                target_w, target_h,
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            )
            
            y = (self.height() - scaled.height()) // 2
            painter.drawPixmap(0, y, scaled)
        
        # Gradient overlay at bottom
        gradient = QLinearGradient(0, self.height() - self._overlay_height, 0, self.height())
        gradient.setColorAt(0, self.palette().color(self.backgroundRole()).darker(0))
        gradient.setColorAt(1, self.palette().color(self.backgroundRole()).darker(200))
        painter.fillRect(0, self.height() - self._overlay_height, self.width(), self._overlay_height, gradient)

        # Title text
        if self._title:
            # 1. Создаем и рисуем мягкое затемнение (градиент снизу вверх)
            gradient = QLinearGradient(0, self.height() - self._overlay_height, 0, self.height())
            # Черный цвет с прозрачностью 120 (из 255) в самом низу, переходящий в прозрачный
            gradient.setColorAt(0.0, QColor(0, 0, 0, 0))  # Сверху оверлея — полностью прозрачно
            gradient.setColorAt(1.0, QColor(0, 0, 0, 120))  # Снизу оверлея — легкое затемнение

            # Закрашиваем область оверлея этим градиентом
            painter.fillRect(
                0, self.height() - self._overlay_height,
                self.width(), self._overlay_height,
                gradient
            )

            # 2. Рисуем сам текст поверх затемнения
            painter.setPen(Qt.GlobalColor.white)
            font = painter.font()
            font.setPointSize(16)
            font.setBold(True)
            painter.setFont(font)
            painter.drawText(
                16, self.height() - self._overlay_height,
                    self.width() - 32, self._overlay_height - 8,
                    Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter,
                self._title,
            )
    
    def resizeEvent(self, event: QResizeEvent) -> None:
        self.updateGeometry()
        super().resizeEvent(event)