from __future__ import annotations

from PySide6.QtCore import QPropertyAnimation, Qt, QTimer
from PySide6.QtWidgets import (
    QDialog,
    QHBoxLayout,
    QLabel,
    QProgressBar,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.core.loading import LoadingState


class ProgressDialog(QDialog):
    """Modal progress dialog showing stacked loading tasks as breadcrumbs."""

    def __init__(self, loading: LoadingState, parent: QWidget | None = None) -> None:
        super().__init__(
            parent,
            Qt.WindowType.WindowStaysOnTopHint
            | Qt.WindowType.CustomizeWindowHint
            | Qt.WindowType.WindowTitleHint,
        )
        self.setWindowTitle("Loading")
        self.setMinimumWidth(420)
        self.setModal(True)

        self._loading = loading
        self._visible = False

        # Breadcrumbs container
        self._crumbs = QWidget()
        self._crumbs_layout = QHBoxLayout(self._crumbs)
        self._crumbs_layout.setContentsMargins(12, 8, 12, 4)
        self._crumbs_layout.setSpacing(4)

        # Progress bar
        self._progress = QProgressBar()
        self._progress.setTextVisible(False)
        self._progress.setFixedHeight(4)
        self._progress.setRange(0, 100)

        # Layout
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        layout.addWidget(self._crumbs)
        layout.addWidget(self._progress)

        # Signals
        loading.changed.connect(self._on_changed)
        loading.finished.connect(self._on_finished)

    @property
    def loading(self) -> LoadingState:
        return self._loading

    async def __aenter__(self) -> ProgressDialog:
        return self

    async def __aexit__(
        self,
        exc_type: type[BaseException] | None,
        exc_val: BaseException | None,
        exc_tb: object,
    ) -> None:
        # Dialog auto-closes via LoadingState.finished signal
        pass

    def _on_changed(self, status: str, pct: int, cur: int, total: int) -> None:
        if not self._visible:
            self._show_animated()

        self._update_breadcrumbs()
        self._progress.setValue(pct)

    def _on_finished(self) -> None:
        QTimer.singleShot(300, self._hide_animated)

    def _update_breadcrumbs(self) -> None:
        # Clear existing
        while True:
            item = self._crumbs_layout.takeAt(0)
            if not item:
                break
            widget = item.widget()
            if widget:
                widget.deleteLater()

        stack = self._loading._stack
        for i, frame in enumerate(stack):
            label = QLabel(frame.status)
            label.setObjectName("breadcrumbItem")
            if i == len(stack) - 1:
                label.setObjectName("breadcrumbCurrent")
            self._crumbs_layout.addWidget(label)

            if i < len(stack) - 1:
                sep = QLabel("▸")
                sep.setObjectName("breadcrumbSep")
                self._crumbs_layout.addWidget(sep)

        self._crumbs_layout.addStretch()

    def _show_animated(self) -> None:
        self._visible = True
        self.setWindowOpacity(0.0)
        self.show()
        self._anim_show = self._animate_opacity(0.0, 1.0, 150, None)

    def _hide_animated(self) -> None:
        if not self._visible:
            return
        self._visible = False
        self._anim_hide = self._animate_opacity(1.0, 0.0, 200, self.hide)

    def _animate_opacity(
        self, start: float, end: float, duration: int, finished_callback: object | None
    ) -> QPropertyAnimation:
        anim = QPropertyAnimation(self, b"windowOpacity", self)
        anim.setDuration(duration)
        anim.setStartValue(start)
        anim.setEndValue(end)
        if finished_callback:
            anim.finished.connect(finished_callback)
        anim.start()
        return anim
