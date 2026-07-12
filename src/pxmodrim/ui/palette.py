from __future__ import annotations

from pathlib import Path

from PySide6.QtGui import QColor

PALETTE: dict[str, str] = {
    "MAIN_BG": "#0f172a",
    "PANEL_BG": "#1e293b",
    "ITEM_HOVER": "#334155",
    "ITEM_ACTIVE": "#334155",
    "BORDER": "#334155",
    "TEXT_MAIN": "#e2e8f0",
    "TEXT_MUTED": "#94a3b8",
    "DESC_TEXT": "#cbd5e1",
    "GREEN": "#22c55e",
    "BLUE": "#3b82f6",
    "BLUE_HOVER": "#2563eb",
    "BUTTON_HOVER": "#475569",
    "WARNING": "#f59e0b",
    "DANGER": "#ef4444",
    "SEARCH_BG": "#0f172a",
    "INPUT_BG": "#0f172a",
    "TAG_BG": "#1e293b",
    "DEP_BG": "#1e293b",
    "PANEL_RGB": "30,41,59",
}

# QColor variants for QPainter delegates
MAIN_BG_Q = QColor(15, 23, 42)
PANEL_BG_Q = QColor(30, 41, 59)
ITEM_HOVER_Q = QColor(51, 65, 85)
ITEM_ACTIVE_Q = QColor(51, 65, 85)
BORDER_Q = QColor(51, 65, 85)
TEXT_MAIN_Q = QColor(226, 232, 240)
TEXT_MUTED_Q = QColor(148, 163, 184)
GREEN_Q = QColor(34, 197, 94)
BLUE_Q = QColor(59, 130, 246)
WARNING_Q = QColor(245, 158, 11)
DANGER_Q = QColor(239, 68, 68)
TAG_BG_Q = QColor(30, 41, 59)
DESC_TEXT_Q = QColor(203, 213, 225)
DEP_BG_Q = QColor(30, 41, 59)
CHECKBOX_BORDER_Q = QColor(71, 85, 105)
CHECKBOX_FILL_Q = QColor(30, 41, 59)
CHECKBOX_CHECK_Q = QColor(59, 130, 246)


def get_stylesheet() -> str:
    qss_path = Path(__file__).parent / "style.qss"
    template = qss_path.read_text()
    return template.format(**PALETTE)
