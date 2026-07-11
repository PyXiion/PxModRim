from __future__ import annotations

from pathlib import Path

from PySide6.QtGui import QColor

PALETTE: dict[str, str] = {
    "MAIN_BG": "#1a1c1e",
    "PANEL_BG": "#212427",
    "ITEM_HOVER": "#2c3135",
    "ITEM_ACTIVE": "#383e44",
    "BORDER": "#2f3438",
    "TEXT_MAIN": "#e3e6e8",
    "TEXT_MUTED": "#9099a2",
    "DESC_TEXT": "#cdd2d6",
    "GREEN": "#4caf50",
    "BLUE": "#00a2ff",
    "BLUE_HOVER": "#008be0",
    "BUTTON_HOVER": "#3c4248",
    "WARNING": "#ffb300",
    "DANGER": "#f44336",
    "SEARCH_BG": "#1e2124",
    "INPUT_BG": "#2d3135",
    "TAG_BG": "#2d352e",
    "DEP_BG": "#292d32",
    "PANEL_RGB": "33,36,39",
}

# QColor variants for QPainter delegates
MAIN_BG_Q = QColor(26, 28, 30)
PANEL_BG_Q = QColor(33, 36, 39)
ITEM_HOVER_Q = QColor(44, 49, 53)
ITEM_ACTIVE_Q = QColor(56, 62, 68)
BORDER_Q = QColor(47, 52, 56)
TEXT_MAIN_Q = QColor(227, 230, 232)
TEXT_MUTED_Q = QColor(144, 153, 162)
GREEN_Q = QColor(76, 175, 80)
BLUE_Q = QColor(0, 162, 255)
WARNING_Q = QColor(255, 179, 0)
DANGER_Q = QColor(244, 67, 54)
TAG_BG_Q = QColor(45, 53, 46)
DESC_TEXT_Q = QColor(205, 210, 214)
DEP_BG_Q = QColor(41, 45, 50)
CHECKBOX_BORDER_Q = QColor(47, 52, 56)
CHECKBOX_FILL_Q = QColor(45, 49, 53)
CHECKBOX_CHECK_Q = QColor(0, 162, 255)


def get_stylesheet() -> str:
    qss_path = Path(__file__).parent / "style.qss"
    template = qss_path.read_text()
    return template.format(**PALETTE)
