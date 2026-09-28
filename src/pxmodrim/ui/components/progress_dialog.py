from __future__ import annotations

from typing import Self

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
from pxmodrim.ui.components.icons import icon
from pxmodrim.ui.theme.palette import PALETTE

_HIDE_DELAY_MS = 300
_FADE_IN_MS = 150
_FADE_OUT_MS = 200


class ProgressDialog(QDialog):
    """Modal progress dialog showing stacked loading tasks as breadcrumbs."""

    def __init__(self, loading: LoadingState, parent: QWidget) -> None:
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
        self._shown = False

        self._crumbs = QWidget(self)
        self._crumbs_layout = QHBoxLayout(self._crumbs)
        self._crumbs_layout.setContentsMargins(0, 0, 0, 8)
        self._crumbs_layout.setSpacing(4)

        self._progress = QProgressBar(self)
        self._progress.setTextVisible(False)
        self._progress.setRange(0, 100)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 12, 12, 12)
        layout.setSpacing(0)
        layout.addWidget(self._crumbs)
        layout.addWidget(self._progress)

        self._hide_timer = QTimer(self)
        self._hide_timer.setSingleShot(True)
        self._hide_timer.setInterval(_HIDE_DELAY_MS)
        self._hide_timer.timeout.connect(self._begin_hide)

        self._fade = QPropertyAnimation(self, b"windowOpacity", self)
        self._fade.finished.connect(self._on_fade_finished)

        loading.changed.connect(self._on_changed)
        loading.finished.connect(self._on_finished)

    @property
    def loading(self) -> LoadingState:
        return self._loading

    async def __aenter__(self) -> Self:
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
        self._hide_timer.stop()
        if not self._shown:
            self._shown = True
            if not self.isVisible():
                self.setWindowOpacity(0.0)
                self.show()
            self._fade_to(1.0, _FADE_IN_MS)

        statuses = self._loading.statuses
        if statuses:
            self.setWindowTitle(statuses[0])
        self._update_breadcrumbs(statuses)
        self._progress.setValue(pct)

    def _on_finished(self) -> None:
        self._hide_timer.start()

    def _update_breadcrumbs(self, statuses: tuple[str, ...]) -> None:
        while (item := self._crumbs_layout.takeAt(0)) is not None:
            widget = item.widget()
            if widget is not None:
                widget.hide()
                widget.deleteLater()

        last = len(statuses) - 1
        for i, status in enumerate(statuses):
            label = QLabel(status, self._crumbs)
            if i == last:
                font = label.font()
                font.setBold(True)
                label.setFont(font)
            self._crumbs_layout.addWidget(label)

            if i < last:
                sep = QLabel(self._crumbs)
                sep.setPixmap(icon("chevron", 12, PALETTE["TEXT_DIM"]).pixmap(12, 12))
                self._crumbs_layout.addWidget(sep)

        self._crumbs_layout.addStretch()

    def _begin_hide(self) -> None:
        self._shown = False
        self._fade_to(0.0, _FADE_OUT_MS)

    def _fade_to(self, end: float, duration: int) -> None:
        self._fade.stop()
        self._fade.setDuration(duration)
        self._fade.setStartValue(self.windowOpacity())
        self._fade.setEndValue(end)
        self._fade.start()

    def _on_fade_finished(self) -> None:
        if not self._shown:
            self.hide()
