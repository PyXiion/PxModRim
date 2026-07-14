from __future__ import annotations

from pathlib import Path
from string import Template

from PySide6.QtGui import QColor

PALETTE: dict[str, str] = {
    "ELEVATE_0": "#0b0d10",
    "ELEVATE_1": "#121418",
    "ELEVATE_2": "#1a1d21",
    "ELEVATE_3": "#22262b",
    "ELEVATE_4": "#2c3036",
    "SURFACE": "#25282e",
    "BORDER": "#2f333a",
    "TEXT_MAIN": "#f2f3f5",
    "TEXT_MUTED": "#949ba4",
    "TEXT_DIM": "#6c737f",
    "PRIMARY": "#66c0f4",
    "PRIMARY_HOVER": "#4aa8d8",
    "PRIMARY_BG": "rgba(102,192,244,0.1)",
    "SUCCESS": "#3ba55d",
    "SUCCESS_BG": "rgba(59,165,93,0.15)",
    "WARNING": "#f9a825",
    "WARNING_BG": "rgba(249,168,37,0.15)",
    "DANGER": "#ed4245",
    "DANGER_BG": "rgba(237,66,69,0.15)",
    "ELEVATE_0_RGB": "11,13,16",
    "ELEVATE_2_RGB": "26,29,33",
}

# QColor variants for QPainter delegates
MAIN_BG_Q = QColor(11, 13, 16)
PANEL_BG_Q = QColor(26, 29, 33)
ITEM_HOVER_Q = QColor(34, 38, 43)
ITEM_ACTIVE_Q = QColor(44, 48, 54)
BORDER_Q = QColor(47, 51, 58)
TEXT_MAIN_Q = QColor(242, 243, 245)
TEXT_MUTED_Q = QColor(148, 155, 164)
TEXT_DIM_Q = QColor(108, 115, 127)
PRIMARY_Q = QColor(102, 192, 244)
SUCCESS_Q = QColor(59, 165, 93)
WARNING_Q = QColor(249, 168, 37)
DANGER_Q = QColor(237, 66, 69)
TAG_BG_Q = QColor(34, 38, 43)
DESC_TEXT_Q = QColor(148, 155, 164)
DEP_BG_Q = QColor(34, 38, 43)
CHECKBOX_BORDER_Q = QColor(47, 51, 58)
CHECKBOX_FILL_Q = QColor(34, 38, 43)
CHECKBOX_CHECK_Q = QColor(102, 192, 244)


def get_stylesheet() -> str:
    qss_path = Path(__file__).parent / "style.qss"
    raw = qss_path.read_text()
    return Template(raw).safe_substitute(PALETTE)
