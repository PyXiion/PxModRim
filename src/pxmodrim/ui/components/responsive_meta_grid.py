from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QResizeEvent
from PySide6.QtWidgets import QGridLayout, QLabel, QLayoutItem, QWidget


class ResponsiveMetaGrid(QWidget):
    """2-column grid that reflows to 1-column on narrow widths."""
    
    BREAKPOINT = 500
    
    def __init__(self, fields: dict[str, str], parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._fields = fields  # {key: label}
        self._labels: dict[str, QLabel] = {}
        self._values: dict[str, QLabel] = {}
        self._grid = QGridLayout(self)
        self._grid.setSpacing(8)
        self._cols = 2
        self._build()
    
    def _build(self) -> None:
        for key, label_text in self._fields.items():
            lbl = QLabel(label_text)
            lbl.setObjectName("metaLabel")
            val = QLabel("—")
            val.setObjectName("metaValue")
            val.setWordWrap(True)
            val.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            self._labels[key] = lbl
            self._values[key] = val
        self._reflow(self.width())
    
    def resizeEvent(self, event: QResizeEvent) -> None:
        new_cols = 1 if event.size().width() < self.BREAKPOINT else 2
        if new_cols != self._cols:
            self._cols = new_cols
            self._reflow(event.size().width())
        super().resizeEvent(event)
    
    def _reflow(self, width: int) -> None:
        while self._grid.count():
            item: QLayoutItem | None = self._grid.takeAt(0)
            if item is not None:
                widget = item.widget()
                if widget is not None:
                    widget.setParent(None)
        
        cols = 1 if width < self.BREAKPOINT else 2
        for i, key in enumerate(self._fields.keys()):
            row = i // cols
            col = i % cols
            self._grid.addWidget(self._labels[key], row, col * 2)
            self._grid.addWidget(self._values[key], row, col * 2 + 1)
    
    def update_values(self, values: dict[str, str]) -> None:
        for key, val in values.items():
            if key in self._values:
                self._values[key].setText(val or "—")