from __future__ import annotations

from PySide6.QtCore import QEvent, QObject, Qt
from PySide6.QtGui import QMouseEvent
from PySide6.QtWidgets import (
    QDialog,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QMessageBox,
    QPushButton,
    QSizeGrip,
    QWidget,
)

from pxmodrim.ui.components.icons import icon
from pxmodrim.ui.theme.palette import PALETTE

_TITLE_BAR_HEIGHT = 36
_BORDER = 1
_GRIP_SIZE = 16


class _TitleBar(QWidget):
    def __init__(self, dialog: QDialog, closable: bool) -> None:
        super().__init__(dialog)
        self.setObjectName("dialogTitleBar")
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        layout = QHBoxLayout(self)
        layout.setContentsMargins(14, 0, 6, 0)
        layout.setSpacing(6)

        self._title = QLabel(dialog.windowTitle(), self)
        self._title.setObjectName("dialogTitleText")
        layout.addWidget(self._title, 1)
        dialog.windowTitleChanged.connect(self._title.setText)

        if closable:
            close_btn = QPushButton(self)
            close_btn.setObjectName("dialogCloseBtn")
            close_btn.setIcon(icon("close", 12, PALETTE["TEXT_MUTED"]))
            close_btn.setFixedSize(26, 26)
            close_btn.setToolTip("Close")
            close_btn.setCursor(Qt.CursorShape.PointingHandCursor)
            close_btn.setFocusPolicy(Qt.FocusPolicy.NoFocus)
            close_btn.clicked.connect(dialog.reject)
            layout.addWidget(close_btn)

    def mousePressEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            handle = self.window().windowHandle()
            if handle is not None:
                handle.startSystemMove()
                event.accept()
                return
        super().mousePressEvent(event)


class _Chrome(QObject):
    """Keeps the overlay title bar and size grip pinned to the dialog frame."""

    def __init__(self, dialog: QDialog, closable: bool, resizable: bool) -> None:
        super().__init__(dialog)
        self._dialog = dialog
        self._bar = _TitleBar(dialog, closable)
        self._grip = QSizeGrip(dialog) if resizable else None
        dialog.installEventFilter(self)
        self._place()

    def _place(self) -> None:
        width = self._dialog.width()
        self._bar.setGeometry(_BORDER, _BORDER, width - 2 * _BORDER, _TITLE_BAR_HEIGHT)
        self._bar.raise_()
        if self._grip is not None:
            self._grip.setGeometry(
                width - _GRIP_SIZE - _BORDER,
                self._dialog.height() - _GRIP_SIZE - _BORDER,
                _GRIP_SIZE,
                _GRIP_SIZE,
            )
            self._grip.raise_()

    def eventFilter(self, watched: QObject, event: QEvent) -> bool:
        if watched is self._dialog and event.type() in (
            QEvent.Type.Resize,
            QEvent.Type.Show,
        ):
            self._place()
        return False


def install_dialog_chrome(
    dialog: QDialog, *, closable: bool = True, resizable: bool | None = None
) -> None:
    """Give *dialog* the frameless title-bar look of the main window.

    Call after the dialog's layout is built and before it is shown. Native file
    pickers keep the OS look. The title bar is an overlay, so the dialog's own
    layout only needs extra top margin, which is added here.
    """
    if isinstance(dialog, QFileDialog) or dialog.property("chromed"):
        return
    if resizable is None:
        resizable = not (isinstance(dialog, QMessageBox) or dialog.isSizeGripEnabled())

    keep = dialog.windowFlags() & Qt.WindowType.WindowStaysOnTopHint
    dialog.setWindowFlags(
        Qt.WindowType.Dialog | Qt.WindowType.FramelessWindowHint | keep
    )
    dialog.setProperty("chromed", True)

    layout = dialog.layout()
    if layout is not None:
        m = layout.contentsMargins()
        layout.setContentsMargins(
            m.left() + _BORDER,
            m.top() + _TITLE_BAR_HEIGHT + _BORDER,
            m.right() + _BORDER,
            m.bottom() + _BORDER,
        )
    _Chrome(dialog, closable, resizable)
