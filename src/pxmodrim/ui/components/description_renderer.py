from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QFrame, QSizePolicy, QTextBrowser, QWidget

from pxmodrim.ui.components.unity_rich_text import unity_rich_text_to_html


class DescriptionRenderer(QTextBrowser):
    """Theme-aware description renderer with internal scroll."""

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)

        self.setObjectName("descriptionBrowser")

        self.setReadOnly(True)
        self.setUndoRedoEnabled(False)
        self.setOpenLinks(False)
        self.setOpenExternalLinks(True)

        self.setFrameShape(QFrame.Shape.NoFrame)

        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)

        self.setSizePolicy(
            QSizePolicy.Policy.Expanding,
            QSizePolicy.Policy.Preferred,
        )

        self.document().setDocumentMargin(0)

        self.document().setDefaultStyleSheet("""
            body {
                font-family: "Segoe UI", system-ui, sans-serif;
                font-size: 13px;
                line-height: 1.4;
                color: #cbd5e1;
                margin: 0;
                padding: 0;
            }

            a {
                color: #3b82f6;
                text-decoration: none;
            }

            a:hover {
                text-decoration: underline;
            }
        """)

        self.hide()

    def clear_description(self) -> None:
        self.clear()
        self.hide()

    def set_description(self, text: str) -> None:
        if not text:
            self.clear_description()
            return

        html_text = unity_rich_text_to_html(text)
        self.setHtml(f"<body>{html_text}</body>")
        self.show()
