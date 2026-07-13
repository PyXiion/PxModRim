from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QGridLayout, QLabel, QSizePolicy, QVBoxLayout, QWidget


class MetaChip(QWidget):
    """Small card showing a label + value pair."""

    def __init__(
        self,
        label: str,
        value: str,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.setObjectName("metaChip")
        self.setSizePolicy(
            QSizePolicy.Policy.MinimumExpanding, QSizePolicy.Policy.Fixed
        )

        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 8, 12, 8)
        layout.setSpacing(2)

        self._label_widget = QLabel(label)
        self._label_widget.setObjectName("metaChipLabel")
        layout.addWidget(self._label_widget)

        self._value_widget = QLabel(value)
        self._value_widget.setObjectName("metaChipValue")
        self._value_widget.setWordWrap(True)
        self._value_widget.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )
        layout.addWidget(self._value_widget)

    def set_value(self, value: str) -> None:
        self._value_widget.setText(value or "—")


class MetaChipRow(QWidget):
    """2-column grid of MetaChip widgets."""

    def __init__(self, fields: dict[str, str], parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._chips: dict[str, MetaChip] = {}
        layout = QGridLayout(self)
        layout.setContentsMargins(16, 12, 16, 12)
        layout.setSpacing(8)

        for i, (key, label) in enumerate(fields.items()):
            chip = MetaChip(label, "—")
            self._chips[key] = chip
            layout.addWidget(chip, i // 2, i % 2)

    def update_values(self, values: dict[str, str]) -> None:
        for key, val in values.items():
            chip = self._chips.get(key)
            if chip:
                chip.set_value(val)
