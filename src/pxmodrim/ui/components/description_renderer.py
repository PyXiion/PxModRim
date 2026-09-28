from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QFrame, QTextBrowser, QWidget

from pxmodrim.ui.components.unity_rich_text import unity_rich_text_to_html
from pxmodrim.ui.theme.palette import PALETTE


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
        self.document().setDocumentMargin(0)
        self.setMinimumHeight(0)

        self.document().setDefaultStyleSheet(
            """
            body {
                font-family: "Source Sans 3", sans-serif;
                font-size: 13px;
                line-height: 1.4;
                color: %TEXT%;
                margin: 0;
                padding: 0;
            }

            a {
                color: %LINK%;
                text-decoration: none;
            }

            a:hover {
                text-decoration: underline;
            }
        """.replace("%TEXT%", PALETTE["TEXT_MUTED"]).replace(
                "%LINK%", PALETTE["PRIMARY"]
            )
        )

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
        self.setFixedHeight(int(self.document().size().height()))
