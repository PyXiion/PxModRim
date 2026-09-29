from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING

from loguru import logger
from PySide6.QtCore import QUrl, Slot
from PySide6.QtGui import QColor
from PySide6.QtQuickWidgets import QQuickWidget
from qasync import asyncSlot

from pxmodrim.core.services.workshop_download_service import workshop_service
from pxmodrim.ui.plugins.downloads.model import DownloadsModel
from pxmodrim.ui.theme.palette import PALETTE
from pxmodrim.ui.views.base import BaseViewPanel

if TYPE_CHECKING:
    from PySide6.QtQml import QQmlEngine
    from PySide6.QtWidgets import QWidget

    from pxmodrim.core.context import CoreContext
    from pxmodrim.core.services.workshop_download_service import DownloadResult
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
        self._workshop = workshop_service(ctx)
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

        if self._workshop is not None:
            self.model.set_busy(self._workshop.is_downloading)
            self._workshop.batch_started.connect(self._on_batch_started)
            self._workshop.download_item_status_changed.connect(self.model.apply)
            self._workshop.download_item_titled.connect(self.model.set_title)
            self._workshop.download_phase_changed.connect(self.model.set_phase)
            self._workshop.download_finished.connect(self._on_finished)
            self._workshop.busy_changed.connect(self.model.set_busy)

    def _on_batch_started(self, ids: list[str]) -> None:
        titles = {
            m.published_file_id: m.name
            for m in self._ctx.all_mods.values()
            if m.published_file_id
        }
        self.model.begin(ids, titles)

    def _on_finished(self, result: DownloadResult) -> None:
        self.model.finish(result)

    @Slot()
    def stop(self) -> None:
        if self._workshop is not None:
            self._workshop.cancel()

    @asyncSlot()
    async def retryFailed(self) -> None:
        ids = self.model.failed_ids()
        if self._workshop is None or not ids:
            return
        logger.info("[workshop] retrying {} failed mods", len(ids))
        try:
            await self._workshop.download_mods(ids)
        except (RuntimeError, ValueError) as exc:
            logger.warning("[workshop] retry not started: {}", exc)
