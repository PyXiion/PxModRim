from __future__ import annotations

from PySide6.QtCore import QEvent, QObject, Qt, Signal
from PySide6.QtGui import QMouseEvent
from PySide6.QtWidgets import QHBoxLayout, QLabel, QPushButton, QWidget

from pxmodrim.ui.components.icons import icon

_WINDOW_CONTROL_SIZE = 32


class TitleBar(QWidget):
    minimize_requested = Signal()
    maximize_requested = Signal()
    close_requested = Signal()

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("titleBar")
        self.setFixedHeight(32)
        self._dragging = False
        self._drag_offset = (0, 0)

        layout = QHBoxLayout(self)
        layout.setContentsMargins(12, 0, 0, 0)
        layout.setSpacing(0)

        self._title_label = QLabel("PxModRim")
        self._title_label.setObjectName("titleBarTitle")
        layout.addWidget(self._title_label)
        layout.addStretch()

        self._min_btn = QPushButton()
        self._max_btn = QPushButton()
        self._close_btn = QPushButton()

        for btn in (self._min_btn, self._max_btn, self._close_btn):
            btn.setFixedSize(_WINDOW_CONTROL_SIZE, 32)
            btn.setObjectName("titleBarBtn")

        self._min_btn.setIcon(icon("minimize", 12, "#949ba4"))
        self._max_btn.setIcon(icon("maximize", 12, "#949ba4"))
        self._close_btn.setIcon(icon("close", 12, "#949ba4"))

        self._min_btn.clicked.connect(self.minimize_requested.emit)
        self._max_btn.clicked.connect(self._on_maximize)
        self._close_btn.clicked.connect(self.close_requested.emit)
        self._close_btn.installEventFilter(self)

        layout.addWidget(self._min_btn)
        layout.addWidget(self._max_btn)
        layout.addWidget(self._close_btn)

    def eventFilter(self, obj: QObject, event: QEvent) -> bool:
        if obj is self._close_btn:
            if event.type() == QEvent.Type.Enter:
                self._close_btn.setIcon(icon("close", 12, "#ffffff"))
            elif event.type() == QEvent.Type.Leave:
                self._close_btn.setIcon(icon("close", 12, "#949ba4"))
        return super().eventFilter(obj, event)

    def update_maximize_icon(self, maximized: bool) -> None:
        self._max_btn.setIcon(
            icon("restore" if maximized else "maximize", 12, "#949ba4")
        )

    def _on_maximize(self) -> None:
        self.maximize_requested.emit()

    def mousePressEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            self._dragging = True
            self._drag_offset = (event.position().x(), event.position().y())
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        if self._dragging and event.buttons() == Qt.MouseButton.LeftButton:
            win = self.window().windowHandle()
            if win is not None:
                win.startSystemMove()
                self._dragging = False
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        self._dragging = False
        super().mouseReleaseEvent(event)

    def mouseDoubleClickEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            self.maximize_requested.emit()
        super().mouseDoubleClickEvent(event)
