from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING

from loguru import logger
from PySide6.QtCore import QUrl, Slot
from PySide6.QtGui import QColor
from PySide6.QtQuickWidgets import QQuickWidget
from qasync import asyncSlot

from pxmodrim.core.downloads import download_manager
from pxmodrim.ui.plugins.downloads.model import DownloadsModel
from pxmodrim.ui.theme.palette import PALETTE
from pxmodrim.ui.views.base import BaseViewPanel

if TYPE_CHECKING:
    from PySide6.QtQml import QQmlEngine
    from PySide6.QtWidgets import QWidget

    from pxmodrim.core.context import CoreContext
    from pxmodrim.core.downloads import DownloadResult
    from pxmodrim.ui.context import AppContext

_QML = Path(__file__).parent / "Downloads.qml"


class DownloadsViewPanel(BaseViewPanel):
    view_id = "downloads"
    icon_name = "download"
    label = "Downloads"

    def __init__(
        self,
        ctx: CoreContext,
        qml_engine: QQmlEngine | None = None,
        parent: QWidget | None = None,
        app_ctx: AppContext | None = None,
    ) -> None:
        super().__init__(ctx, qml_engine, parent, app_ctx=app_ctx)
        self._downloads = download_manager(ctx)
        self.model = DownloadsModel(self)

        self._qml = QQuickWidget(qml_engine, self)  # type: ignore[arg-type]
        self._qml.setObjectName("downloadsView")
        self._qml.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        self._qml.setClearColor(QColor(PALETTE["ELEVATE_0"]))
        qml_ctx = self._qml.rootContext()
        qml_ctx.setContextProperty("downloadsPanel", self)
        qml_ctx.setContextProperty("downloadsModel", self.model)
        self._qml.setSource(QUrl.fromLocalFile(str(_QML)))
        self._root.addWidget(self._qml, 1)

        self.model.set_busy(self._downloads.is_downloading)
        self._downloads.batch_started.connect(self._on_batch_started)
        self._downloads.download_item_status_changed.connect(self.model.apply)
        self._downloads.download_item_titled.connect(self.model.set_title)
        self._downloads.download_phase_changed.connect(self.model.set_phase)
        self._downloads.download_finished.connect(self._on_finished)
        self._downloads.busy_changed.connect(self.model.set_busy)

    def _on_batch_started(self, ids: list[str]) -> None:
        self.model.begin(ids, self._downloads.names_by_id(self._ctx.all_mods.values()))

    def _on_finished(self, result: DownloadResult) -> None:
        self.model.finish(result)

    @Slot()
    def stop(self) -> None:
        self._downloads.cancel()

    @asyncSlot()
    async def retryFailed(self) -> None:
        ids = self.model.failed_ids()
        if not ids:
            return
        logger.info("[downloads] retrying {} failed mods", len(ids))
        try:
            await self._downloads.download_mods(ids)
        except (RuntimeError, ValueError) as exc:
            logger.warning("[downloads] retry not started: {}", exc)
