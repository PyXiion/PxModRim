from __future__ import annotations

from PySide6.QtCore import QPropertyAnimation, Qt, QTimer
from PySide6.QtWidgets import (
    QGraphicsOpacityEffect,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.ui.components.icons import icon
from pxmodrim.ui.palette import PALETTE

_TOAST_LEVELS = {
    "info": ("info", PALETTE["PRIMARY"]),
    "success": ("toast-success", PALETTE["SUCCESS"]),
    "warning": ("toast-warning", PALETTE["WARNING"]),
    "error": ("toast-error", PALETTE["DANGER"]),
}


class Toast(QWidget):
    def __init__(
        self,
        message: str,
        level: str = "info",
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.setObjectName("toast")
        self.setFixedWidth(320)
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents, False)

        icon_name, accent = _TOAST_LEVELS.get(level, _TOAST_LEVELS["info"])

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
        close_btn.setIcon(icon("close", 10, "#6c737f"))
        close_btn.setFixedSize(20, 20)
        close_btn.clicked.connect(self.hide)
        layout.addWidget(close_btn)

        self._opacity_anim = QPropertyAnimation(self._opacity_effect, b"opacity")
        self._opacity_anim.setDuration(200)
        self._opacity_anim.setStartValue(0.0)
        self._opacity_anim.setEndValue(1.0)

    def show_with_animation(self) -> None:
        self.show()
        self.raise_()
        self._opacity_anim.setDirection(QPropertyAnimation.Direction.Forward)
        self._opacity_anim.start()

    def hide_with_animation(self) -> None:
        self._opacity_anim.setDirection(QPropertyAnimation.Direction.Backward)
        self._opacity_anim.finished.connect(self.hide)
        self._opacity_anim.start()


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
        level: str = "info",
        duration_ms: int = 3000,
    ) -> None:
        toast = Toast(message, level, self)
        self._layout.addWidget(toast)
        self._active_toasts.append(toast)

        toast.show_with_animation()

        if duration_ms > 0:

            def _dismiss(toast=toast) -> None:
                toast.hide()
                self._layout.removeWidget(toast)
                toast.deleteLater()
                if toast in self._active_toasts:
                    self._active_toasts.remove(toast)

            QTimer.singleShot(duration_ms, _dismiss)

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
