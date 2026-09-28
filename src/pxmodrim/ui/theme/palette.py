from __future__ import annotations

from pathlib import Path
from string import Template

from PySide6.QtGui import QColor


def _tint(r: int, g: int, b: int, alpha: float) -> tuple[str, str]:
    """(QSS rgba() string, QML #AARRGGBB string) for one translucent color."""
    qml = f"#{round(alpha * 255):02x}{r:02x}{g:02x}{b:02x}"
    return f"rgba({r},{g},{b},{alpha})", qml


_PRIMARY_BG = _tint(102, 192, 244, 0.1)
_SUCCESS_BG = _tint(59, 165, 93, 0.15)
_WARNING_BG = _tint(249, 168, 37, 0.15)
_DANGER_BG = _tint(237, 66, 69, 0.15)

# *_BG entries are rgba() for QSS only; QML rejects rgba() strings (renders opaque
# black), so QML consumers must use the *_BG_QML entries.
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
    "TEXT_ON_ACCENT": "#0b0d10",
    "NEUTRAL": "#6b7280",
    "PRIMARY": "#66c0f4",
    "PRIMARY_HOVER": "#4aa8d8",
    "PRIMARY_BG": _PRIMARY_BG[0],
    "PRIMARY_BG_QML": _PRIMARY_BG[1],
    "SUCCESS": "#3ba55d",
    "SUCCESS_HOVER": "#2f8a4c",
    "SUCCESS_BG": _SUCCESS_BG[0],
    "SUCCESS_BG_QML": _SUCCESS_BG[1],
    "WARNING": "#f9a825",
    "WARNING_BG": _WARNING_BG[0],
    "WARNING_BG_QML": _WARNING_BG[1],
    "DANGER": "#ed4245",
    "DANGER_HOVER": "#d1383b",
    "DANGER_BG": _DANGER_BG[0],
    "DANGER_BG_QML": _DANGER_BG[1],
}

# QColor variants for QPainter delegates
PANEL_BG_Q = QColor(PALETTE["ELEVATE_2"])
TEXT_MAIN_Q = QColor(PALETTE["TEXT_MAIN"])
TEXT_MUTED_Q = QColor(PALETTE["TEXT_MUTED"])


def get_stylesheet() -> str:
    qss_path = Path(__file__).parent / "style.qss"
    raw = qss_path.read_text()
    return Template(raw).safe_substitute(PALETTE)
