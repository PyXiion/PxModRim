from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Signal, Slot
from PySide6.QtGui import QColor
from PySide6.QtQml import QQmlEngine
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import QVBoxLayout, QWidget

from pxmodrim.ui.theme.palette import PALETTE
from pxmodrim.ui.views.steam_workshop.download_queue_model import (
    DownloadQueueModel,
)

_QML_DIR = Path(__file__).parent
_DL_SIDEBAR_QML = _QML_DIR / "DownloadSidebar.qml"


class DownloadSidebar(QWidget):
    download_requested = Signal()
    item_removed = Signal(str)
    stop_requested = Signal()
    clear_requested = Signal()

    def __init__(
        self, qml_engine: QQmlEngine | None = None, parent: QWidget | None = None
    ) -> None:
        super().__init__(parent)
        self.setObjectName("downloadSidebar")
        self.setFixedWidth(240)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        self._model = DownloadQueueModel(self)

        self._qml = QQuickWidget(qml_engine, self)  # type: ignore[arg-type]
        self._qml.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        self._qml.setClearColor(QColor(PALETTE["ELEVATE_2"]))

        qml_ctx = self._qml.rootContext()
        qml_ctx.setContextProperty("downloadSidebar", self)
        qml_ctx.setContextProperty("downloadQueueModel", self._model)
        self._qml.setSource(str(_DL_SIDEBAR_QML))

        layout.addWidget(self._qml)

    @Slot()
    def downloadRequested(self) -> None:
        self.download_requested.emit()

    @Slot()
    def stopRequested(self) -> None:
        self.stop_requested.emit()

    @Slot(str)
    def removeItem(self, mod_id: str) -> None:
        self.item_removed.emit(mod_id)

    @Slot()
    def clearQueue(self) -> None:
        self.clear_requested.emit()

    def set_download_enabled(self, enabled: bool) -> None:
        root = self._qml.rootObject()
        if root is not None:
            root.setProperty("downloadEnabled", enabled)

    def sync_from(
        self,
        checked_ids: dict[str, str],
        statuses: dict[str, str] | None = None,
    ) -> None:
        self._model.sync_from(checked_ids, statuses)

    def set_progress(
        self, total: int, completed: int, downloading_id: str = ""
    ) -> None:
        self._model.set_progress(total, completed, downloading_id)

    def update_status(self, mod_id: str, status: str) -> None:
        self._model.update_status(mod_id, status)
