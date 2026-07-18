from __future__ import annotations

from typing import Literal

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QAbstractButton,
    QButtonGroup,
    QHBoxLayout,
    QPushButton,
    QStackedWidget,
    QVBoxLayout,
    QWidget,
)

from pxmodrim.ui.theme.palette import PALETTE

Orientation = Literal["horizontal", "vertical"]


class _TabButton(QPushButton):
    def __init__(
        self,
        icon_name: str,
        label: str,
        orientation: Orientation,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._icon_name = icon_name
        self._orientation = orientation
        self._active = False

        from pxmodrim.ui.components.icons import icon

        self.setIcon(icon(icon_name, 14, "#949ba4"))
        self.setText(label)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setCheckable(True)

        self._apply_style()

    def _apply_style(self) -> None:
        from pxmodrim.ui.components.icons import icon

        color = "#f2f3f5" if self._active else "#949ba4"
        self.setIcon(icon(self._icon_name, 14, color))

        if self._orientation == "horizontal":
            self.setFixedHeight(30)
            self.setStyleSheet(f"""
                QPushButton {{
                    background: {PALETTE['ELEVATE_1']};
                    border: none;
                    color: #949ba4;
                    font-size: 11px;
                    font-weight: 500;
                    padding: 6px 14px;
                    text-align: left;
                }}
                QPushButton:hover {{
                    background: {PALETTE['ELEVATE_4']};
                }}
                QPushButton:checked {{
                    background: {PALETTE['ELEVATE_2']};
                    color: {PALETTE['TEXT_MAIN']};
                    border-top: 2px solid {PALETTE['PRIMARY']};
                    border-left: 1px solid {PALETTE['BORDER']};
                    border-right: 1px solid {PALETTE['BORDER']};
                    border-bottom: none;
                    border-top-left-radius: 4px;
                    border-top-right-radius: 4px;
                    border-bottom-left-radius: 0px;
                    border-bottom-right-radius: 0px;
                }}
            """)
        else:
            self.setFixedHeight(34)
            self.setStyleSheet(f"""
                QPushButton {{
                    background: {PALETTE['ELEVATE_1']};
                    border: none;
                    color: #949ba4;
                    font-size: 11px;
                    font-weight: 500;
                    padding: 6px 14px;
                    text-align: left;
                }}
                QPushButton:hover {{
                    background: {PALETTE['ELEVATE_4']};
                }}
                QPushButton:checked {{
                    background: {PALETTE['ELEVATE_2']};
                    color: {PALETTE['TEXT_MAIN']};
                    border-left: 2px solid {PALETTE['PRIMARY']};
                    border-top: 1px solid {PALETTE['BORDER']};
                    border-bottom: 1px solid {PALETTE['BORDER']};
                    border-right: none;
                    border-top-left-radius: 4px;
                    border-bottom-left-radius: 4px;
                    border-top-right-radius: 0px;
                    border-bottom-right-radius: 0px;
                }}
            """)

    def set_active(self, active: bool) -> None:
        self._active = active
        self.setChecked(active)
        self._apply_style()


class IconTabWidget(QWidget):
    currentChanged = Signal(int)

    def __init__(
        self,
        orientation: Orientation = "horizontal",
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._orientation: Orientation = orientation
        self._buttons: list[_TabButton] = []
        self._current_index = -1
        self._button_group = QButtonGroup(self)
        self._button_group.setExclusive(True)
        self._button_group.buttonClicked.connect(self._on_button_clicked)

        if orientation == "horizontal":
            self._root = QVBoxLayout(self)
            self._root.setContentsMargins(0, 0, 0, 0)
            self._root.setSpacing(0)

            self._tab_bar = QWidget()
            self._tab_bar.setStyleSheet(
                f"background: {PALETTE['ELEVATE_2']}; border: none;"
            )
            self._tab_layout = QHBoxLayout(self._tab_bar)
            self._tab_layout.setContentsMargins(0, 0, 0, 0)
            self._tab_layout.setSpacing(0)

            self._stack = QStackedWidget()
            self._stack.setStyleSheet(
                f"background: {PALETTE['ELEVATE_2']}; border: none;"
            )

            self._root.addWidget(self._tab_bar)
            self._root.addWidget(self._stack, 1)
        else:
            self._root = QHBoxLayout(self)
            self._root.setContentsMargins(0, 0, 0, 0)
            self._root.setSpacing(0)

            self._tab_bar = QWidget()
            self._tab_bar.setFixedWidth(140)
            self._tab_bar.setStyleSheet(
                f"background: {PALETTE['ELEVATE_2']}; border: none;"
            )
            self._tab_layout = QVBoxLayout(self._tab_bar)
            self._tab_layout.setContentsMargins(0, 0, 0, 0)
            self._tab_layout.setSpacing(0)
            self._tab_layout.addStretch(1)

            self._stack = QStackedWidget()
            self._stack.setStyleSheet(
                f"background: {PALETTE['ELEVATE_2']}; border: none;"
            )

            self._root.addWidget(self._tab_bar)
            self._root.addWidget(self._stack, 1)

    def addTab(
        self, widget: QWidget, icon_name: str, label: str
    ) -> int:
        index = self._stack.count()
        self._stack.addWidget(widget)

        btn = _TabButton(icon_name, label, self._orientation)
        self._button_group.addButton(btn, index)

        if self._orientation == "vertical":
            insert_pos = self._tab_layout.count() - 1
            self._tab_layout.insertWidget(insert_pos, btn)
        else:
            self._tab_layout.addWidget(btn)

        self._buttons.append(btn)

        if self._current_index == -1:
            self._set_current(0)

        return index

    def _on_button_clicked(self, button: QAbstractButton) -> None:
        index = self._button_group.id(button)
        if index >= 0:
            self._set_current(index)

    def _set_current(self, index: int) -> None:
        if index == self._current_index:
            return

        if self._current_index >= 0 and self._current_index < len(self._buttons):
            self._buttons[self._current_index].set_active(False)

        self._current_index = index
        self._stack.setCurrentIndex(index)

        if index >= 0 and index < len(self._buttons):
            self._buttons[index].set_active(True)

        self.currentChanged.emit(index)

    def currentIndex(self) -> int:
        return self._current_index

    def setCurrentIndex(self, index: int) -> None:
        self._set_current(index)

    def widget(self, index: int) -> QWidget | None:
        return self._stack.widget(index)

    def count(self) -> int:
        return self._stack.count()
