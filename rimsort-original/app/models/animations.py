import asyncio
from collections.abc import Callable, Generator
from contextlib import contextmanager

from loguru import logger
from PySide6.QtCore import (
    QByteArray,
    QEasingCurve,
    QEvent,
    QPropertyAnimation,
    Qt,
    QTimer,
)
from PySide6.QtGui import QColor, QMovie, QPainter
from PySide6.QtWidgets import (
    QApplication,
    QDialog,
    QGraphicsOpacityEffect,
    QLabel,
    QVBoxLayout,
    QWidget,
)


class AnimationLabel(QLabel):
    """
    Subclass for QLabel. Displays fading text on the bottom
    status panel.
    """

    def __init__(self) -> None:
        """
        Prepare the QLabel to have its opacity
        changed through a timed animation.
        """
        super(AnimationLabel, self).__init__()
        self.effect = QGraphicsOpacityEffect()
        self.effect.setOpacity(0)
        self.setGraphicsEffect(self.effect)
        self.animation = QPropertyAnimation(self.effect, QByteArray(b"opacity"))
        self.timer = QTimer()
        self.timer.setInterval(1000)
        self.timer.setSingleShot(True)
        self.timer.timeout.connect(self.fade)

    def fade(self) -> None:
        """
        Start an animation for fading out the text.
        """
        self.animation.stop()
        self.animation.setDuration(300)
        self.animation.setStartValue(1)
        self.animation.setEndValue(0)
        self.animation.setEasingCurve(QEasingCurve.Type.Linear)
        self.animation.start()

    def start_pause_fade(self, text: str) -> None:
        """
        Start the timer for calling the fade animation.
        The text should be displayed normally for 5 seconds,
        after which the fade animation is called.

        :param text: the string to display and fade
        """
        self.clear()
        if self.timer.isActive():
            self.timer.stop()
            self.animation.stop()
        self.setText(text)
        self.effect.setOpacity(1)
        self.setGraphicsEffect(self.effect)
        self.timer.start(5000)


class LoadingDialog(QDialog):
    """Frameless modal overlay with a loading GIF and text label.

    Tiles over the entire parent window with a semi-transparent dark
    background, blocking all input while the animation plays.
    """

    def __init__(
        self,
        gif_path: str,
        text: str = "",
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.setObjectName("loadingOverlay")
        self.setWindowFlags(Qt.WindowType.FramelessWindowHint | Qt.WindowType.Dialog)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.setWindowModality(Qt.WindowModality.WindowModal)

        self._bg_color = QColor(0, 0, 0, 200)

        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignTop)
        layout.setSpacing(16)

        self._movie = QMovie(gif_path)
        self._gif_label = QLabel(self)
        self._gif_label.setMovie(self._movie)
        self._gif_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._gif_label.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        layout.addWidget(self._gif_label)

        self._text_container = QWidget(self)
        self._text_container.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self._text_layout = QVBoxLayout(self._text_container)
        self._text_layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self._text_container)

        if text:
            self.add_text(text)

        self._movie.start()

        self._tile_over_parent()
        if parent is not None:
            parent.installEventFilter(self)

    def _tile_over_parent(self) -> None:
        p = self.parentWidget()
        if p is not None and p.geometry().width() > 0:
            g = p.geometry()
            self.setGeometry(g.x(), g.y(), g.width(), g.height())
        else:
            screen = QApplication.primaryScreen()
            if screen is not None:
                sg = screen.availableGeometry()
                self.setGeometry(sg.x(), sg.y(), sg.width(), sg.height())
            else:
                self.resize(800, 600)

    def paintEvent(self, event: object) -> None:  # noqa: ARG002
        painter = QPainter(self)
        painter.fillRect(self.rect(), self._bg_color)

    def add_text(self, text: str) -> None:
        label = QLabel(text, self._text_container)
        label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        label.setObjectName("loadingOverlayText")
        label.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self._text_layout.addWidget(label)
        QApplication.processEvents()

    def update_text_at(self, index: int, text: str) -> None:
        count = self._text_layout.count()
        if 0 <= index < count:
            item = self._text_layout.itemAt(index)
            if item and item.widget():
                item.widget().setText(text)

    def update_last_text(self, text: str) -> None:
        self.update_text_at(self._text_layout.count() - 1, text)

    def remove_last_text(self) -> None:
        if self._text_layout.count() > 0:
            item = self._text_layout.takeAt(self._text_layout.count() - 1)
            if item and item.widget():
                item.widget().deleteLater()
            # QApplication.processEvents()

    def set_gif(self, gif_path: str) -> None:
        """Change the displayed GIF animation."""
        self._movie.stop()
        self._movie = QMovie(gif_path)
        self._gif_label.setMovie(self._movie)
        self._movie.start()

    def eventFilter(self, obj: object, event: object) -> bool:
        if (
            obj is self.parentWidget()
            and isinstance(event, QEvent)
            and event.type() == QEvent.Type.Resize
        ):
            self._tile_over_parent()
        return super().eventFilter(obj, event)

    def closeEvent(self, event: object) -> None:  # noqa: ARG002
        self._movie.stop()
        if self.parentWidget() is not None:
            self.parentWidget().removeEventFilter(self)
        super().closeEvent(event)


class LoadingState:
    """Context object returned by ``loading_state()`` for progress updates.

    Usage::

        with manager.loading_state("Processing...") as state:
            state.set_total(100)
            for i in range(100):
                state.step()
    """

    def __init__(self, manager: "LoadingOverlayManager", text: str, index: int) -> None:
        self._manager = manager
        self._text = text
        self._index = index
        self._current = 0
        self._total = 0

    def set_total(self, total: int) -> None:
        self._total = total
        self._update_label()

    def set_progress(self, current: int, total: int | None = None) -> None:
        if total is not None:
            self._total = total
        self._current = current
        self._update_label()

    def step(self, n: int = 1) -> None:
        self._current += n
        self._update_label()

    def _update_label(self) -> None:
        dialog = self._manager._dialog
        if dialog is not None:
            dialog.update_text_at(
                self._index, f"{self._text} ({self._current}/{self._total})"
            )


class LoadingOverlayManager:
    """Stack-based loading overlay manager.

    Manages a modal ``LoadingDialog`` with a stack of text labels
    displayed as a column.
    On the first :meth:`push` the dialog is created and shown.
    Each subsequent :meth:`push` adds a new label to the column.
    :meth:`pop` removes the last label; the last pop closes
    the dialog and unblocks the UI.

    Use :meth:`loading_state` as a context manager for safe
    push/pop pairs that always unwind, even on exceptions.
    """

    def __init__(self, gif_path: str) -> None:
        self._gif_path = gif_path
        self._dialog: LoadingDialog | None = None
        self._stack: list[str] = []

    def _create_dialog(self, text: str) -> None:
        parent = QApplication.activeWindow()
        self._dialog = LoadingDialog(
            gif_path=self._gif_path,
            text=text,
            parent=parent,
        )
        self._dialog.show()

    def push(self, text: str, gif_path: str | None = None) -> None:
        self._stack.append(text)
        if self._dialog is None:
            if gif_path:
                self._gif_path = gif_path
            self._create_dialog(text)
            # self._dialog.set_gif(gif_path)
        else:
            self._dialog.add_text(text)
            if gif_path is not None and gif_path != self._gif_path:
                self._dialog.set_gif(gif_path)
                self._gif_path = gif_path

    def pop(self) -> None:
        if not self._stack:
            return
        self._stack.pop()
        if self._stack:
            self._dialog.remove_last_text()
        else:
            self._close()

    def _close(self) -> None:
        if self._dialog is not None:
            self._dialog.close()
            self._dialog = None

    def fire_and_forget(
        self,
        target: Callable[[], object],
        text: str = "",
        gif_path: str | None = None,
    ) -> None:
        """Show overlay with *text*, run *target* in a thread, close when done.

        Non-blocking — the caller returns immediately and the overlay
        closes automatically once *target* completes.
        """
        self.push(text, gif_path)
        try:
            loop = asyncio.get_running_loop()
        except RuntimeError:
            QTimer.singleShot(0, lambda: self.fire_and_forget(target, text, gif_path))
            return
        task = loop.create_task(self._run_and_pop(target))
        task.add_done_callback(
            lambda _t: None
        )  # suppress "exception was never retrieved"

    async def _run_and_pop(self, target: Callable[[], object]) -> None:
        try:
            loop = asyncio.get_running_loop()
            await loop.run_in_executor(None, target)
        except Exception:
            logger.exception("fire_and_forget task failed")
        finally:
            self.pop()

    @contextmanager
    def loading_state(
        self, text: str, gif_path: str | None = None
    ) -> Generator[LoadingState, None, None]:
        index = len(self._stack)
        state = LoadingState(self, text, index)
        self.push(text, gif_path)
        try:
            yield state
        finally:
            self.pop()


# ── Module-level singleton ────────────────────────────────────────

_shared_loading_manager: LoadingOverlayManager | None = None


def get_loading_manager() -> LoadingOverlayManager:
    """Return the shared LoadingOverlayManager instance (lazily created)."""
    global _shared_loading_manager
    if _shared_loading_manager is None:
        from app.utils.app_info import AppInfo

        gif_path = str(AppInfo().theme_data_folder / "default-icons" / "rimsort.gif")
        _shared_loading_manager = LoadingOverlayManager(gif_path=gif_path)
    return _shared_loading_manager
