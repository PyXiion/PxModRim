from __future__ import annotations

import asyncio
from collections.abc import Awaitable, Callable
from pathlib import Path
from typing import TYPE_CHECKING, Any, cast

from PySide6.QtCore import Property, QUrl, Signal, Slot
from PySide6.QtGui import QColor, QDesktopServices
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import QDialog
from qasync import asyncSlot

from pxmodrim.core.downloads import download_manager
from pxmodrim.core.workshop import (
    CatalogCollection,
    CatalogError,
    CatalogMod,
    CatalogQuery,
    DownloadPlan,
    WorkshopCatalog,
)
from pxmodrim.ui.components.dialogs import await_dialog
from pxmodrim.ui.panels.settings_panel import SettingsPanel
from pxmodrim.ui.plugins.downloads.model import DownloadsModel
from pxmodrim.ui.plugins.workshop.models import CatalogListModel, item_row, safe_url
from pxmodrim.ui.theme.palette import PALETTE
from pxmodrim.ui.views.base import BaseViewPanel

if TYPE_CHECKING:
    from PySide6.QtQml import QQmlEngine
    from PySide6.QtWidgets import QWidget

    from pxmodrim.core.context import CoreContext
    from pxmodrim.core.downloads import DownloadProgress, DownloadResult
    from pxmodrim.ui.context import AppContext


class WorkshopViewPanel(BaseViewPanel):
    view_id = "workshop"
    icon_name = "steam"
    label = "Workshop"
    changed = Signal()
    queueChanged = Signal()

    def __init__(
        self,
        ctx: CoreContext,
        qml_engine: QQmlEngine | None = None,
        parent: QWidget | None = None,
        app_ctx: AppContext | None = None,
        *,
        catalog: WorkshopCatalog | None = None,
    ) -> None:
        super().__init__(ctx, qml_engine, parent, app_ctx=app_ctx)
        self._catalog = catalog or cast(
            WorkshopCatalog, ctx.plugins.get("workshop_catalog")
        )
        self._downloads = download_manager(ctx)
        self.mods_model = CatalogListModel(self)
        self.collections_model = CatalogListModel(self)
        self.members_model = CatalogListModel(self)
        self.queue_model = DownloadsModel(self)
        self._tab = "Discover"
        self._busy = False
        self._error = ""
        self._notice = ""
        self._detail: dict[str, Any] = {}
        self._query = ""
        self._version = "" if ctx.target_version == "Unknown" else ctx.target_version
        self._tag = ""
        self._sort = "popular"
        self._source = "all"
        self._generation = 0
        self._retry: Callable[[], Awaitable[None]] | None = None
        self._installed: list[CatalogMod] = []
        self._pending: DownloadPlan | None = None
        self._titles: dict[str, str] = {}
        self._items: dict[str, CatalogMod | CatalogCollection] = {}
        self._queue_progress = 0.0
        self._queue_summary = "No downloads yet"
        self._qml = QQuickWidget(qml_engine, self)  # type: ignore[arg-type]
        self._qml.setObjectName("workshopView")
        self._qml.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        self._qml.setClearColor(QColor(PALETTE["ELEVATE_0"]))
        qml_ctx = self._qml.rootContext()
        for name, value in (
            ("workshopPanel", self),
            ("catalogMods", self.mods_model),
            ("catalogCollections", self.collections_model),
            ("catalogMembers", self.members_model),
            ("workshopQueue", self.queue_model),
        ):
            qml_ctx.setContextProperty(name, value)
        self._qml.setSource(
            QUrl.fromLocalFile(str(Path(__file__).with_name("Workshop.qml")))
        )
        self._root.addWidget(self._qml, 1)
        self.queue_model.set_busy(self._downloads.is_downloading)
        self._downloads.batch_started.connect(self._on_batch_started)
        self._downloads.download_progress.connect(self._on_progress)
        self._downloads.download_item_status_changed.connect(self.queue_model.apply)
        self._downloads.download_item_titled.connect(self.queue_model.set_title)
        self._downloads.download_phase_changed.connect(self.queue_model.set_phase)
        self._downloads.download_finished.connect(self._on_finished)
        self._downloads.busy_changed.connect(self.queue_model.set_busy)
        self._catalog.catalog_url_changed.connect(self._on_url_changed)
        self._catalog.installed_changed.connect(self._on_installed_changed)

    def _row(self, item: CatalogMod | CatalogCollection) -> dict[str, Any]:
        self._items[item.id] = item
        version = (
            "" if self._ctx.target_version == "Unknown" else self._ctx.target_version
        )
        return item_row(item, self._catalog.install_state, version)

    async def _run(
        self, fetch: Callable[[], Awaitable[Any]], apply: Callable[[Any], None]
    ) -> None:
        self._generation += 1
        generation = self._generation
        self._busy, self._error = True, ""
        self.changed.emit()

        async def retry() -> None:
            await self._run(fetch, apply)

        self._retry = retry
        try:
            result = await fetch()
            if generation == self._generation:
                apply(result)
        except CatalogError as exc:
            if generation == self._generation:
                self._error = exc.message
        except (RuntimeError, ValueError) as exc:
            if generation == self._generation:
                self._error = str(exc)
        finally:
            if generation == self._generation:
                self._busy = False
                self.changed.emit()

    async def load(self, *, append: bool = False) -> None:
        if not self._catalog.configured:
            self._generation += 1
            self._busy = False
            self.changed.emit()
            return
        self._detail = {}
        model = (
            self.collections_model if self._tab == "Collections" else self.mods_model
        )
        query = CatalogQuery(
            query=self._query,
            tag=self._tag or None,
            version=self._version or None,
            sort=self._sort,
            source=self._source,
            cursor=model.cursor if append else None,
        )
        if self._tab == "Discover":

            def apply_discover(result: Any) -> None:
                self.mods_model.set_page(
                    [self._row(item) for item in result.mods.items],
                    result.mods.total,
                    None,
                )
                self.collections_model.set_page(
                    [self._row(item) for item in result.collections.items],
                    result.collections.total,
                    None,
                )

            await self._run(self._catalog.discover, apply_discover)
        elif self._tab == "Installed":

            def apply_installed(items: list[CatalogMod]) -> None:
                self._installed = items
                rows = [
                    self._row(item)
                    for item in items
                    if not self._query
                    or self._query.casefold() in item.title.casefold()
                ]
                self.mods_model.set_page(rows, len(rows), None)

            await self._run(self._catalog.installed_with_updates, apply_installed)
        else:
            fetch = (
                self._catalog.collections
                if self._tab == "Collections"
                else self._catalog.mods
            )

            def apply_page(page: Any) -> None:
                model.set_page(
                    [self._row(item) for item in page.items],
                    page.total,
                    page.next_cursor,
                    append=append,
                )

            await self._run(lambda: fetch(query), apply_page)

    @asyncSlot()
    async def refresh(self) -> None:
        await self.load()

    @asyncSlot(str)
    async def selectTab(self, tab: str) -> None:
        self._tab, self._notice, self._pending = tab, "", None
        await self.load()

    @asyncSlot(str, str, str, str, str)
    async def filter(
        self, query: str, version: str, source: str, sort: str, tag: str
    ) -> None:
        self._query, self._version, self._source, self._sort, self._tag = (
            query,
            version,
            source,
            sort,
            tag,
        )
        if self._tab == "Discover":
            self._tab = "Mods"
        await self.load()

    @asyncSlot()
    async def loadMore(self) -> None:
        await self.load(append=True)

    @asyncSlot()
    async def retry(self) -> None:
        if self._retry:
            await self._retry()

    @asyncSlot(str, str)
    async def openItem(self, item_id: str, kind: str) -> None:
        await self.open_item(item_id, kind)

    async def open_item(self, item_id: str, kind: str) -> None:
        async def fetch() -> tuple[dict[str, Any], list[dict[str, Any]]]:
            if kind == "collection":
                detail = await self._catalog.collection(item_id)
                if detail is None:
                    raise CatalogError("This collection is no longer available.", 404)
                row = self._row(detail.collection)
                row["complete"] = detail.is_complete
                row["warning"] = (
                    ""
                    if detail.is_complete
                    else "This collection is incomplete. "
                    "Some members are unavailable or not listed."
                )
                if detail.unavailable_ids:
                    row["warning"] += " Unavailable: " + ", ".join(
                        detail.unavailable_ids
                    )
                members = [self._row(item) for item in detail.members]
                row["actionLabel"] = (
                    "Complete download"
                    if any(member["state"] != "missing" for member in members)
                    else "Download all"
                )
                return row, members
            mod = await self._catalog.mod(item_id)
            if mod is None:
                raise CatalogError("This mod is no longer available.", 404)
            dependencies = await asyncio.gather(
                *(self._catalog.mod(pid) for pid in mod.dependencies)
            )
            row = self._row(mod)
            missing = [
                pid
                for pid, item in zip(mod.dependencies, dependencies, strict=True)
                if item is None
            ]
            row["warning"] = (
                "Unavailable dependencies: " + ", ".join(missing) if missing else ""
            )
            row["complete"] = not missing
            return row, [self._row(item) for item in dependencies if item is not None]

        def apply(result: tuple[dict[str, Any], list[dict[str, Any]]]) -> None:
            self._detail, members = result
            self.members_model.set_page(members, len(members), None)

        await self._run(fetch, apply)

    @Slot()
    def back(self) -> None:
        self._generation += 1
        self._detail, self._error, self._busy = {}, "", False
        self.changed.emit()

    async def _download(self, ids: list[str], collections: list[str]) -> None:
        self._notice, self._pending = "", None

        async def fetch() -> DownloadPlan:
            plan = await self._catalog.plan(ids, collections)
            self._titles.update(plan.titles)
            if plan.is_complete and plan.to_download:
                await self._catalog.download(plan)
            return plan

        def apply(plan: DownloadPlan) -> None:
            if not plan.is_complete:
                self._pending = plan
                parts = ["The download plan is incomplete."]
                if plan.unavailable_ids:
                    parts.append("Unavailable: " + ", ".join(plan.unavailable_ids))
                if plan.incomplete_collection_ids:
                    parts.append(
                        "Incomplete collections: "
                        + ", ".join(plan.incomplete_collection_ids)
                    )
                self._notice = " ".join(parts)
            elif not plan.to_download:
                self._notice = (
                    "All available mods are already installed and up to date."
                )
            else:
                self._notice = (
                    "Download finished. See Downloads for item results. "
                    "Mods were not activated."
                )

        await self._run(fetch, apply)

    @asyncSlot(str, str)
    async def downloadItem(self, item_id: str, kind: str) -> None:
        await self._download(
            [item_id] if kind == "mod" else [],
            [item_id] if kind == "collection" else [],
        )

    @asyncSlot()
    async def updateAll(self) -> None:
        ids = [
            item.id
            for item in self._installed
            if self._catalog.install_state(item) == "outdated"
        ]
        if ids:
            await self._download(ids, [])

    @asyncSlot()
    async def downloadAvailable(self) -> None:
        plan, self._pending = self._pending, None
        if plan and plan.to_download:
            await self._run(
                lambda: self._catalog.download(plan),
                lambda _: setattr(
                    self,
                    "_notice",
                    "Download finished. See Downloads for item results.",
                ),
            )

    @Slot()
    def dismissPlan(self) -> None:
        self._pending, self._notice = None, ""
        self.changed.emit()

    @Slot(str)
    def openLink(self, url: str) -> None:
        if safe_url(url):
            QDesktopServices.openUrl(QUrl(url))

    @Slot()
    def openDownloads(self) -> None:
        if self._app_ctx:
            self._app_ctx.navigate("downloads")

    @asyncSlot()
    async def openSettings(self) -> None:
        if self._qml_engine is None:
            return
        result, dialog = await await_dialog(
            SettingsPanel, self._ctx, self._qml_engine, self
        )
        if result == QDialog.DialogCode.Accepted:
            self._ctx.update_config(dialog.get_config())
        dialog.deleteLater()

    @Slot()
    def stop(self) -> None:
        self._downloads.cancel()

    def _on_batch_started(self, ids: list[str]) -> None:
        titles = self._downloads.names_by_id(self._ctx.all_mods.values()) | self._titles
        self.queue_model.begin(ids, titles)
        self._queue_progress = 0.0
        self._queue_summary = f"Downloading {len(ids)} mods"
        self.queueChanged.emit()

    def _on_progress(self, progress: DownloadProgress) -> None:
        self._queue_progress = (
            progress.completed / progress.total if progress.total else 0.0
        )
        self._queue_summary = f"{progress.completed} / {progress.total} mods"
        self.queueChanged.emit()

    def _on_finished(self, result: DownloadResult) -> None:
        self.queue_model.finish(result)
        self._queue_summary = (
            f"{len(result.succeeded)} completed · {len(result.failed)} failed"
        )
        self.queueChanged.emit()

    @asyncSlot(str)
    async def _on_url_changed(self, _url: str) -> None:
        self._detail, self._error, self._notice, self._pending = {}, "", "", None
        self._items.clear()
        self.mods_model.set_page([], 0, None)
        self.collections_model.set_page([], 0, None)
        self.members_model.set_page([], 0, None)
        await self.load()

    @asyncSlot(object)
    async def _on_installed_changed(self, _value: None) -> None:
        def convert(row: dict[str, Any]) -> dict[str, Any]:
            return self._row(self._items[row["itemId"]])

        for model in (self.mods_model, self.collections_model, self.members_model):
            model.refresh_rows(convert)
        if self._detail:
            item = self._items[self._detail["itemId"]]
            self._detail.update(self._row(item))
            if isinstance(item, CatalogCollection):
                members = [self._items.get(pid) for pid in item.member_ids]
                partially_installed = any(
                    isinstance(member, CatalogMod)
                    and self._catalog.install_state(member) != "missing"
                    for member in members
                )
                self._detail["actionLabel"] = (
                    "Complete download" if partially_installed else "Download all"
                )
        self.changed.emit()
        if self._tab == "Installed" and not self._busy:
            await self.load()

    @Property(str, notify=changed)  # type: ignore[arg-type]
    def tab(self) -> str:
        return self._tab

    @Property(bool, notify=changed)  # type: ignore[arg-type]
    def configured(self) -> bool:
        return self._catalog.configured

    @Property(bool, notify=changed)  # type: ignore[arg-type]
    def busy(self) -> bool:
        return self._busy

    @Property(str, notify=changed)  # type: ignore[arg-type]
    def error(self) -> str:
        return self._error

    @Property(str, notify=changed)  # type: ignore[arg-type]
    def notice(self) -> str:
        return self._notice

    @Property(dict, notify=changed)  # type: ignore[arg-type]
    def detail(self) -> dict[str, Any]:
        return self._detail

    @Property(bool, notify=changed)  # type: ignore[arg-type]
    def hasDetail(self) -> bool:
        return bool(self._detail)

    @Property(bool, notify=changed)  # type: ignore[arg-type]
    def pendingPlan(self) -> bool:
        return self._pending is not None

    @Property(bool, notify=changed)  # type: ignore[arg-type]
    def canDownloadAvailable(self) -> bool:
        return self._pending is not None and bool(self._pending.to_download)

    @Property(str, constant=True)  # type: ignore[arg-type]
    def gameVersion(self) -> str:
        return "" if self._ctx.target_version == "Unknown" else self._ctx.target_version

    @Property(list, constant=True)  # type: ignore[arg-type]
    def versionOptions(self) -> list[str]:
        return list(
            dict.fromkeys(
                [
                    "All versions",
                    *([self._version] if self._version else []),
                    "1.6",
                    "1.5",
                    "1.4",
                    "1.3",
                ]
            )
        )

    @Property(int, notify=changed)  # type: ignore[arg-type]
    def updateCount(self) -> int:
        return sum(
            self._catalog.install_state(item) == "outdated" for item in self._installed
        )

    @Property(float, notify=queueChanged)  # type: ignore[arg-type]
    def queueProgress(self) -> float:
        return self._queue_progress

    @Property(str, notify=queueChanged)  # type: ignore[arg-type]
    def queueSummary(self) -> str:
        return self._queue_summary
