from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QFrame, QTextBrowser, QWidget


class DescriptionRenderer(QTextBrowser):
    """Theme-aware description renderer with internal scroll when content exceeds max height."""
    
    MAX_HEIGHT = 200
    
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("descriptionBrowser")
        self.setOpenExternalLinks(True)
        self.setFrameStyle(QFrame.Shape.NoFrame)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setSizePolicy(
            self.sizePolicy().Policy.Expanding,
            self.sizePolicy().Policy.Minimum,
        )
        self.document().setDocumentMargin(0)
    
    def set_description(self, text: str) -> None:
        if not text:
            self.hide()
            return
        
        # Escape HTML entities, preserve line breaks
        escaped = (
            text.replace("&", "&")
                .replace("<", "<")
                .replace(">", ">")
        )
        html = escaped.replace("\n", "<br>")
        
        self.document().setDefaultStyleSheet("""
            body {
                font-family: 'Segoe UI', system-ui, sans-serif;
                font-size: 13px;
                line-height: 1.5;
                color: #cdd2d6;
                margin: 0;
                padding: 0;
            }
            a { color: #00a2ff; text-decoration: none; }
            a:hover { text-decoration: underline; }
        """)
        
        self.setHtml(f"<div>{html}</div>")
        self.show()
        
        # Adjust height based on content
        doc_height = self.document().size().height()
        if doc_height > self.MAX_HEIGHT:
            self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
            self.setFixedHeight(self.MAX_HEIGHT)
        else:
            self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
            self.setFixedHeight(int(doc_height) + 16)