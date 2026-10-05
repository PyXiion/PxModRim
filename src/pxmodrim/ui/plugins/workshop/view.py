from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator, Awaitable, Callable
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Any, cast

from loguru import logger
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
from pxmodrim.ui.plugins.workshop.models import CatalogListModel, item_row, safe_url
from pxmodrim.ui.theme.palette import PALETTE
from pxmodrim.ui.views.base import BaseViewPanel

if TYPE_CHECKING:
    from PySide6.QtQml import QQmlEngine
    from PySide6.QtWidgets import QWidget

    from pxmodrim.core.context import CoreContext
    from pxmodrim.ui.context import AppContext


_PAGE = 48


@dataclass(frozen=True)
class _Detail:
    item: CatalogMod | CatalogCollection
    members: tuple[CatalogMod | CatalogCollection, ...]
    warning: str
    complete: bool


class WorkshopViewPanel(BaseViewPanel):
    view_id = "workshop"
    icon_name = "steam"
    label = "Workshop"
    changed = Signal()

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
        self._planning = 0
        self._update_count = 0
        self._installed_view: list[CatalogMod] = []
        self._installed_shown = _PAGE
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
        ):
            qml_ctx.setContextProperty(name, value)
        self._qml.setSource(
            QUrl.fromLocalFile(str(Path(__file__).with_name("Workshop.qml")))
        )
        self._root.addWidget(self._qml, 1)
        self._downloads.busy_changed.connect(self._on_queue_changed)
        self._catalog.queue_changed.connect(self._on_queue_changed)
        self._catalog.catalog_url_changed.connect(self._on_url_changed)
        self._catalog.installed_changed.connect(self._on_installed_changed)

    def _row(self, item: CatalogMod | CatalogCollection) -> dict[str, Any]:
        self._items[item.id] = item
        version = (
            "" if self._ctx.target_version == "Unknown" else self._ctx.target_version
        )
        row = item_row(item, self._catalog.install_state, version)
        if isinstance(item, CatalogMod) and row["state"] != "installed":
            running = item.id in self._downloads.active_ids
            if running or item.id in self._catalog.queued_ids:
                row["queued"] = True
                row["actionLabel"] = "Downloading" if running else "Queued"
        return row

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

    async def _run_stream(
        self,
        stream: Callable[[], AsyncIterator[Any]],
        apply: Callable[[Any], None],
    ) -> None:
        self._generation += 1
        generation = self._generation
        self._busy, self._error = True, ""
        self.changed.emit()

        async def retry() -> None:
            await self._run_stream(stream, apply)

        self._retry = retry
        shown = False
        try:
            async for result in stream():
                if generation != self._generation:
                    return
                apply(result)
                shown = True
                self._busy = False
                self.changed.emit()
        except (CatalogError, RuntimeError, ValueError) as exc:
            message = exc.message if isinstance(exc, CatalogError) else str(exc)
            if generation == self._generation:
                if shown:
                    logger.warning("[workshop] background refresh failed: {}", message)
                else:
                    self._error = message
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
        if append and self._tab == "Installed":
            self._show_more_installed()
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

            await self._run_stream(
                lambda: self._catalog.cached("discover", self._catalog.discover),
                apply_discover,
            )
        elif self._tab == "Installed":

            def apply_installed(items: list[CatalogMod]) -> None:
                self._installed = items
                self._recount()
                self._render_installed()

            await self._run_stream(
                lambda: self._catalog.cached(
                    "installed", self._catalog.installed_with_updates, fresh_for=0
                ),
                apply_installed,
            )
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

            if append:
                await self._run(lambda: fetch(query), apply_page)
            else:
                await self._run_stream(
                    lambda: self._catalog.cached(
                        f"{self._tab}|{query!r}", lambda: fetch(query)
                    ),
                    apply_page,
                )

    def _render_installed(self) -> None:
        needle = self._query.casefold()
        self._installed_view = [
            item
            for item in self._installed
            if not needle
            or needle
            in " ".join(
                (item.title, item.author.name or "", item.id, *item.tags)
            ).casefold()
        ]
        shown = self._installed_view[: self._installed_shown]
        self.mods_model.set_page(
            [self._row(item) for item in shown],
            len(self._installed_view),
            "more" if len(shown) < len(self._installed_view) else None,
        )

    def _show_more_installed(self) -> None:
        start = self.mods_model.rowCount()
        chunk = self._installed_view[start : start + _PAGE]
        self._installed_shown = start + len(chunk)
        self.mods_model.set_page(
            [self._row(item) for item in chunk],
            len(self._installed_view),
            "more" if self._installed_shown < len(self._installed_view) else None,
            append=True,
        )

    @asyncSlot()
    async def refresh(self) -> None:
        await self.load()

    @asyncSlot(str)
    async def selectTab(self, tab: str) -> None:
        self._tab, self._notice, self._pending = tab, "", None
        self._installed_shown = _PAGE
        await self.load()

    @asyncSlot(str, str, str, str, str)
    async def filter(
        self, query: str, version: str, source: str, sort: str, tag: str
    ) -> None:
        self._installed_shown = _PAGE
        self._query, self._version, self._source, self._sort, self._tag = (
            query,
            version,
            source,
            sort,
            tag,
        )
        if self._tab == "Installed" and self._installed:
            self._detail = {}
            self._render_installed()
            self.changed.emit()
            return
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
        async def fetch() -> _Detail:
            if kind == "collection":
                detail = await self._catalog.collection(item_id)
                if detail is None:
                    raise CatalogError("This collection is no longer available.", 404)
                warning = (
                    ""
                    if detail.is_complete
                    else "This collection is incomplete. "
                    "Some members are unavailable or not listed."
                )
                if detail.unavailable_ids:
                    warning += " Unavailable: " + ", ".join(detail.unavailable_ids)
                return _Detail(
                    detail.collection,
                    tuple(detail.members),
                    warning,
                    detail.is_complete,
                )
            mod = await self._catalog.mod(item_id)
            if mod is None:
                raise CatalogError("This mod is no longer available.", 404)
            dependencies = await asyncio.gather(
                *(self._catalog.mod(pid) for pid in mod.dependencies)
            )
            missing = [
                pid
                for pid, item in zip(mod.dependencies, dependencies, strict=True)
                if item is None
            ]
            return _Detail(
                mod,
                tuple(item for item in dependencies if item is not None),
                "Unavailable dependencies: " + ", ".join(missing) if missing else "",
                not missing,
            )

        def apply(detail: _Detail) -> None:
            row = self._row(detail.item)
            members = [self._row(item) for item in detail.members]
            row["complete"], row["warning"] = detail.complete, detail.warning
            if isinstance(detail.item, CatalogCollection):
                row["actionLabel"] = (
                    "Complete download"
                    if any(member["state"] != "missing" for member in members)
                    else "Download all"
                )
            self._detail = row
            self.members_model.set_page(members, len(members), None)

        await self._run_stream(
            lambda: self._catalog.cached(f"detail|{kind}|{item_id}", fetch), apply
        )

    @Slot()
    def back(self) -> None:
        self._generation += 1
        self._detail, self._error, self._busy = {}, "", False
        self.changed.emit()

    def _queue_plan(self, plan: DownloadPlan) -> None:
        self._catalog.enqueue(plan)
        count = len(plan.to_download)
        self._notice = (
            f"Queued {count} mod{'s' if count != 1 else ''}. Progress is shown "
            "in the header and results in Downloads. Mods are not activated."
        )

    async def _download(self, ids: list[str], collections: list[str]) -> None:
        self._notice, self._pending = "Preparing download…", None
        self._planning += 1
        self.changed.emit()
        try:
            plan = await self._catalog.plan(ids, collections)
        except (CatalogError, RuntimeError, ValueError) as exc:
            message = exc.message if isinstance(exc, CatalogError) else str(exc)
            self._notice = f"Could not prepare the download: {message}"
            return
        finally:
            self._planning -= 1
            self.changed.emit()
        self._titles.update(plan.titles)
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
            self._notice = "All available mods are already installed and up to date."
        else:
            self._queue_plan(plan)
        self.changed.emit()

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
            self._queue_plan(plan)
        self.changed.emit()

    @Slot()
    def dismissPlan(self) -> None:
        self._pending, self._notice = None, ""
        self.changed.emit()

    @Slot(str)
    def openLink(self, url: str) -> None:
        if safe_url(url):
            QDesktopServices.openUrl(QUrl(url))

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

    def _on_queue_changed(self, *_: object) -> None:
        self._refresh_rows()
        self.changed.emit()

    @asyncSlot(str)
    async def _on_url_changed(self, _url: str) -> None:
        self._detail, self._error, self._notice, self._pending = {}, "", "", None
        self._items.clear()
        self.mods_model.set_page([], 0, None)
        self.collections_model.set_page([], 0, None)
        self.members_model.set_page([], 0, None)
        await self.load()

    def _recount(self) -> None:
        self._update_count = sum(
            self._catalog.install_state(item) == "outdated" for item in self._installed
        )

    def _refresh_rows(self) -> None:
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

    @asyncSlot(object)
    async def _on_installed_changed(self, _value: None) -> None:
        self._recount()
        self._refresh_rows()
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
        return self._update_count

    @Property(bool, notify=changed)  # type: ignore[arg-type]
    def planning(self) -> bool:
        return self._planning > 0
