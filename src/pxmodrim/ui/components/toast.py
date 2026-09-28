from __future__ import annotations

from typing import Literal

from PySide6.QtCore import QPropertyAnimation, Qt, QTimer, Signal
from PySide6.QtWidgets import (
    QGraphicsOpacityEffect,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.ui.components.icons import icon
from pxmodrim.ui.theme.palette import PALETTE

ToastLevel = Literal["info", "success", "warning", "error"]

_TOAST_LEVELS: dict[ToastLevel, tuple[str, str]] = {
    "info": ("info", PALETTE["PRIMARY"]),
    "success": ("check-circle", PALETTE["SUCCESS"]),
    "warning": ("warning", PALETTE["WARNING"]),
    "error": ("error", PALETTE["DANGER"]),
}


class Toast(QWidget):
    dismissed = Signal()

    def __init__(
        self,
        message: str,
        level: ToastLevel = "info",
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.setObjectName("toast")
        self.setFixedWidth(320)
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents, False)

        icon_name, accent = _TOAST_LEVELS[level]

        self._opacity_effect = QGraphicsOpacityEffect(self)
        self._opacity_effect.setOpacity(0.0)
        self.setGraphicsEffect(self._opacity_effect)

        layout = QHBoxLayout(self)
        layout.setContentsMargins(12, 10, 8, 10)
        layout.setSpacing(8)

        icon_label = QLabel()
        icon_label.setPixmap(icon(icon_name, 16, accent).pixmap(16, 16))
        icon_label.setFixedSize(16, 16)
        layout.addWidget(icon_label)

        self._message_label = QLabel(message)
        self._message_label.setObjectName("toastMessage")
        self._message_label.setWordWrap(True)
        layout.addWidget(self._message_label, 1)

        close_btn = QPushButton()
        close_btn.setObjectName("toastClose")
        close_btn.setIcon(icon("close", 10, PALETTE["TEXT_DIM"]))
        close_btn.setFixedSize(20, 20)
        close_btn.setToolTip("Dismiss")
        close_btn.clicked.connect(self.dismiss)
        layout.addWidget(close_btn)

        self._opacity_anim = QPropertyAnimation(self._opacity_effect, b"opacity")
        self._opacity_anim.setDuration(200)
        self._opacity_anim.setStartValue(0.0)
        self._opacity_anim.setEndValue(1.0)

        self._fade_out_anim = QPropertyAnimation(self._opacity_effect, b"opacity", self)
        self._fade_out_anim.setDuration(150)
        self._fade_out_anim.setEndValue(0.0)
        self._fade_out_anim.finished.connect(self.dismissed)
        self._dismissing = False

    def show_with_animation(self) -> None:
        self.show()
        self.raise_()
        self._opacity_anim.setDirection(QPropertyAnimation.Direction.Forward)
        self._opacity_anim.start()

    def dismiss(self) -> None:
        if self._dismissing:
            return
        self._dismissing = True
        self._opacity_anim.stop()
        self._fade_out_anim.setStartValue(self._opacity_effect.opacity())
        self._fade_out_anim.start()


class ToastManager(QWidget):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("toastManager")
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents, True)
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, False)

        self._layout = QVBoxLayout(self)
        self._layout.setContentsMargins(0, 0, 16, 16)
        self._layout.setSpacing(8)
        self._layout.setAlignment(
            Qt.AlignmentFlag.AlignBottom | Qt.AlignmentFlag.AlignRight
        )

        self._active_toasts: list[Toast] = []

    def show_toast(
        self,
        message: str,
        level: ToastLevel = "info",
        duration_ms: int = 3000,
    ) -> None:
        toast = Toast(message, level, self)
        self._layout.addWidget(toast)
        self._active_toasts.append(toast)
        toast.dismissed.connect(lambda: self._remove(toast))

        toast.show_with_animation()

        if duration_ms > 0:
            QTimer.singleShot(duration_ms, toast, toast.dismiss)

    def _remove(self, toast: Toast) -> None:
        toast.hide()
        self._layout.removeWidget(toast)
        toast.deleteLater()
        if toast in self._active_toasts:
            self._active_toasts.remove(toast)

    def info(self, message: str, duration: int = 3000) -> None:
        self.show_toast(message, "info", duration)

    def success(self, message: str, duration: int = 3000) -> None:
        self.show_toast(message, "success", duration)

    def warning(self, message: str, duration: int = 4000) -> None:
        self.show_toast(message, "warning", duration)

    def error(self, message: str, duration: int = 5000) -> None:
        self.show_toast(message, "error", duration)

    def resize_to_parent(self) -> None:
        parent = self.parentWidget()
        if parent:
            self.setGeometry(parent.rect())
