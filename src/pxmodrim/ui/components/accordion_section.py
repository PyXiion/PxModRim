from __future__ import annotations

from PySide6.QtCore import (
    Property,
    QEasingCurve,
    QEvent,
    QObject,
    QPropertyAnimation,
    QSize,
    Qt,
    Signal,
)
from PySide6.QtGui import QMouseEvent, QShowEvent
from PySide6.QtWidgets import QHBoxLayout, QLabel, QVBoxLayout, QWidget

QWIDGETSIZE_MAX = 16777215


class _AccordionHeader(QWidget):
    clicked = Signal()

    def __init__(self, title: str, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("accordionHeader")
        self.setCursor(Qt.CursorShape.PointingHandCursor)

        layout = QHBoxLayout(self)
        layout.setContentsMargins(20, 10, 20, 10)
        layout.setSpacing(8)

        self._chevron = QLabel("\u25b6")
        self._chevron.setObjectName("accordionChevron")
        self._chevron.setFixedWidth(16)
        self._chevron.setAlignment(Qt.AlignmentFlag.AlignCenter)

        self._title = QLabel(title)
        self._title.setObjectName("accordionTitle")

        layout.addWidget(self._chevron)
        layout.addWidget(self._title, 1)

    def mousePressEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            self.clicked.emit()
        super().mousePressEvent(event)

    def set_title(self, title: str) -> None:
        self._title.setText(title)

    def title_text(self) -> str:
        return self._title.text()

    def set_expanded(self, expanded: bool) -> None:
        self._chevron.setText("\u25bc" if expanded else "\u25b6")


class AccordionSection(QWidget):
    toggled = Signal(bool)

    def __init__(
        self,
        title: str,
        content: QWidget,
        expanded: bool = False,
        parent: QWidget | None = None,
        _animation_duration: int = 200,
    ) -> None:
        super().__init__(parent)
        self._content = content
        self._expanded = expanded

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        self._header = _AccordionHeader(title)
        self._header.clicked.connect(self.toggle)
        layout.addWidget(self._header)

        self._content_wrapper = QWidget()
        self._content_wrapper.setObjectName("accordionContent")
        cw_layout = QVBoxLayout(self._content_wrapper)
        cw_layout.setContentsMargins(20, 0, 20, 0)
        cw_layout.setSpacing(0)
        cw_layout.setAlignment(Qt.AlignmentFlag.AlignTop)
        cw_layout.addWidget(content)

        self._content_height: int = 0
        if expanded:
            content.show()
            self._content_height = self._target_height()
        else:
            content.hide()
        self._content_wrapper.setMaximumHeight(self._content_height)
        layout.addWidget(self._content_wrapper)

        self._anim = QPropertyAnimation(self, b"contentHeight")
        self._anim.setDuration(_animation_duration)
        self._anim.setEasingCurve(QEasingCurve.Type.OutCubic)
        self._anim.finished.connect(self._on_anim_finished)

        content.installEventFilter(self)
        self._content_wrapper.installEventFilter(self)

    # ---- overrides ----

    def showEvent(self, event: QShowEvent) -> None:
        if self._expanded and self._anim.state() == QPropertyAnimation.State.Stopped:
            new_h = self._target_height()
            if new_h != self._content_height:
                self._content_height = new_h
                self._content_wrapper.setMaximumHeight(QWIDGETSIZE_MAX)
                self.updateGeometry()
        super().showEvent(event)

    def sizeHint(self) -> QSize:
        h = self._header.sizeHint().height() + self._content_height
        return QSize(self._header.sizeHint().width(), h)

    def minimumSizeHint(self) -> QSize:
        return self._header.minimumSizeHint()

    def eventFilter(self, obj: QObject, event: QEvent) -> bool:
        if (
            (obj is self._content or obj is self._content_wrapper)
            and event.type() == QEvent.Type.LayoutRequest
            and self._expanded
            and self._anim.state() == QPropertyAnimation.State.Stopped
        ):
            new_h = self._target_height()
            if new_h != self._content_height:
                self._content_height = new_h
                self._content_wrapper.setMaximumHeight(QWIDGETSIZE_MAX)
                self.updateGeometry()
        return super().eventFilter(obj, event)

    # ---- content height property for animation ----

    def _get_content_height(self) -> int:
        return self._content_height

    def _set_content_height(self, h: int) -> None:
        self._content_height = h
        self._content_wrapper.setMaximumHeight(h)
        self.updateGeometry()

    contentHeight = Property(int, _get_content_height, _set_content_height)

    # ---- public API ----

    def toggle(self) -> None:
        self._expanded = not self._expanded
        self._header.set_expanded(self._expanded)
        self._anim.stop()
        if self._expanded:
            self._content.show()
            end = self._target_height()
        else:
            end = 0
        start = self._content_height
        self._anim.setStartValue(start)
        self._anim.setEndValue(end)
        self._anim.start()
        self.toggled.emit(self._expanded)

    def set_expanded(self, expanded: bool) -> None:
        if expanded != self._expanded:
            self.toggle()

    def is_expanded(self) -> bool:
        return self._expanded

    def set_title(self, title: str) -> None:
        self._header.set_title(title)

    def title_text(self) -> str:
        return self._header.title_text()

    # ---- internal ----

    def _target_height(self) -> int:
        wrapper = self._content_wrapper
        layout = wrapper.layout()
        if layout is not None:
            layout.activate()
        width = wrapper.width()
        if width > 0:
            hfw = wrapper.heightForWidth(width)
            if hfw > 0:
                return hfw
        return wrapper.sizeHint().height()

    def _on_anim_finished(self) -> None:
        if self._expanded:
            self._content_wrapper.setMaximumHeight(QWIDGETSIZE_MAX)
        else:
            self._content.hide()
