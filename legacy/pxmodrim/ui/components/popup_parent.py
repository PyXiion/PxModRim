from __future__ import annotations

from PySide6.QtCore import QEvent, QObject
from PySide6.QtQuickWidgets import QQuickWidget


class QuickPopupParenting(QObject):
    """Gives every QQuickWidget's offscreen window its top-level window as transient
    parent when shown. Without it Wayland refuses ``Popup.Window`` popups (menus,
    combo box lists, tooltips): "Failed to create popup … transientParent".
    """

    def eventFilter(self, watched: QObject, event: QEvent) -> bool:
        if event.type() == QEvent.Type.Show and isinstance(watched, QQuickWidget):
            top = watched.window().windowHandle()
            if top is not None:
                watched.quickWindow().setTransientParent(top)
        return False
