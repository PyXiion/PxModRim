from __future__ import annotations

from PySide6.QtCore import (
    QEasingCurve,
    QEvent,
    QObject,
    QPropertyAnimation,
    Qt,
    Signal,
)
from PySide6.QtGui import QMouseEvent
from PySide6.QtWidgets import QHBoxLayout, QLabel, QVBoxLayout, QWidget


class _AccordionHeader(QWidget):
    """Clickable header widget for accordion."""
    
    clicked = Signal()
    
    def __init__(self, title: str, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("accordionHeader")
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        
        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 10, 12, 10)
        layout.setSpacing(0)
        
        self._chevron = QLabel("▶")
        self._chevron.setObjectName("accordionChevron")
        self._chevron.setFixedWidth(16)
        self._chevron.setAlignment(Qt.AlignmentFlag.AlignCenter)
        
        self._title_label = QLabel(title)
        self._title_label.setObjectName("accordionTitle")
        
        h_layout = QHBoxLayout()
        h_layout.setContentsMargins(0, 0, 0, 0)
        h_layout.setSpacing(8)
        h_layout.addWidget(self._chevron)
        h_layout.addWidget(self._title_label, 1)
        
        layout.addLayout(h_layout)
    
    def mousePressEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            self.clicked.emit()
        super().mousePressEvent(event)
    
    def set_title(self, title: str) -> None:
        self._title_label.setText(title)
    
    def title_text(self) -> str:
        return self._title_label.text()
    
    def set_expanded(self, expanded: bool) -> None:
        self._chevron.setText("▼" if expanded else "▶")


class AccordionSection(QWidget):
    """Animated collapsible section with chevron indicator."""
    
    toggled = Signal(bool)
    
    def __init__(
        self,
        title: str,
        content: QWidget,
        expanded: bool = False,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._content = content
        self._expanded = expanded
        self._target_height = 0
        self._animating = False
        
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        
        # Header
        self._header = _AccordionHeader(title)
        self._header.clicked.connect(self.toggle)
        layout.addWidget(self._header)
        
        # Content wrapper (clips during animation)
        self._content_wrapper = QWidget()
        self._content_wrapper.setObjectName("accordionContent")
        cw_layout = QVBoxLayout(self._content_wrapper)
        cw_layout.setContentsMargins(12, 8, 12, 12)
        cw_layout.setSpacing(0)
        cw_layout.addWidget(content)
        self._content_wrapper.setMaximumHeight(0 if not expanded else self._calc_target_height())
        layout.addWidget(self._content_wrapper)
        
        # Animation
        self._anim = QPropertyAnimation(self._content_wrapper, b"maximumHeight")
        self._anim.setDuration(200)
        self._anim.setEasingCurve(QEasingCurve.Type.OutCubic)
        self._anim.finished.connect(self._on_anim_finished)
        
        # Track content size changes
        content.installEventFilter(self)
    
    def _calc_target_height(self) -> int:
        layout = self._content_wrapper.layout()
        if layout is not None:
            layout.activate()
        return self._content_wrapper.sizeHint().height()
    
    def _update_chevron(self) -> None:
        self._header.set_expanded(self._expanded)
    
    def toggle(self) -> None:
        self._expanded = not self._expanded
        self._update_chevron()
        self._target_height = self._calc_target_height() if self._expanded else 0
        self._anim.setStartValue(self._content_wrapper.maximumHeight())
        self._anim.setEndValue(self._target_height)
        self._anim.start()
        self.toggled.emit(self._expanded)
    
    def set_expanded(self, expanded: bool) -> None:
        if expanded != self._expanded:
            self.toggle()
    
    def _on_anim_finished(self) -> None:
        if self._expanded:
            self._content_wrapper.setMaximumHeight(16777215)  # QWIDGETSIZE_MAX
    
    def eventFilter(self, obj: QObject, event: QEvent) -> bool:
        if obj is self._content and event.type() == QEvent.Type.LayoutRequest:
            if self._expanded:
                self._target_height = self._calc_target_height()
                self._content_wrapper.setMaximumHeight(self._target_height)
        return super().eventFilter(obj, event)