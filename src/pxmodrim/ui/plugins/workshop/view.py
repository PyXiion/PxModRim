from __future__ import annotations

from collections.abc import AsyncIterator, Awaitable, Callable, Iterable
from pathlib import Path
from typing import TYPE_CHECKING, Any, cast

import msgspec
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
    CatalogPage,
    CatalogQuery,
    Discover,
    DownloadPlan,
    WorkshopCatalog,
)
from pxmodrim.core.workshop.catalog import FRESH_SECONDS
from pxmodrim.ui.components.dialogs import await_dialog
from pxmodrim.ui.panels.settings_panel import SettingsPanel
from pxmodrim.ui.plugins.workshop.details import (
    Detail,
    fetch_detail,
    incomplete_plan_notice,
    queued_notice,
)
from pxmodrim.ui.plugins.workshop.installed import InstalledList
from pxmodrim.ui.plugins.workshop.models import CatalogListModel, item_row, safe_url
from pxmodrim.ui.theme.palette import PALETTE
from pxmodrim.ui.views.base import BaseViewPanel

if TYPE_CHECKING:
    from PySide6.QtQml import QQmlEngine
    from PySide6.QtWidgets import QWidget

    from pxmodrim.core.context import CoreContext
    from pxmodrim.ui.context import AppContext

_FAILURES = (CatalogError, RuntimeError, ValueError)


def _message(exc: Exception) -> str:
    return exc.message if isinstance(exc, CatalogError) else str(exc)


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
        self._criteria = CatalogQuery(version=self._game_version or None)
        self._busy = False
        self._error = ""
        self._notice = ""
        self._generation = 0
        self._retry: Callable[[], Awaitable[None]] | None = None
        self._detail: dict[str, Any] = {}
        self._detail_data: Detail | None = None
        self._installed = InstalledList()
        self._update_count = 0
        self._pending: DownloadPlan | None = None
        self._planning = 0
        self._items: dict[str, CatalogMod | CatalogCollection] = {}
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

    @property
    def _game_version(self) -> str:
        version = self._ctx.target_version
        return "" if version == "Unknown" else version

    # -- rows -------------------------------------------------------------

    def _row(self, item: CatalogMod | CatalogCollection) -> dict[str, Any]:
        self._items[item.id] = item
        row = item_row(item, self._catalog.install_state, self._game_version)
        if isinstance(item, CatalogMod) and row["state"] != "installed":
            running = item.id in self._downloads.active_ids
            if running or item.id in self._catalog.queued_ids:
                row["queued"] = True
                row["actionLabel"] = "Downloading" if running else "Queued"
        return row

    def _rows(self, items: Iterable[CatalogMod | CatalogCollection]) -> list[Any]:
        return [self._row(item) for item in items]

    def _detail_row(self, detail: Detail) -> dict[str, Any]:
        row = self._row(detail.item)
        row["complete"], row["warning"] = detail.complete, detail.warning
        if isinstance(detail.item, CatalogCollection):
            if not row["previewUrl"] and not row["collage"]:
                for member in detail.members:
                    if not isinstance(member, CatalogMod):
                        continue
                    preview = safe_url(
                        member.preview_url
                        or next(
                            (p.url for p in member.previews if p.type == "image"), ""
                        )
                    )
                    if preview:
                        row["collage"].append(preview)
                    if len(row["collage"]) == 4:
                        break
            started = any(
                isinstance(member, CatalogMod)
                and self._catalog.install_state(member) != "missing"
                for member in detail.members
            )
            row["actionLabel"] = "Complete download" if started else "Download all"
        return row

    def _refresh_rows(self) -> None:
        for model in (self.mods_model, self.collections_model, self.members_model):
            model.refresh_rows(lambda row: self._row(self._items[row["itemId"]]))
        if self._detail_data is not None:
            self._detail = self._detail_row(self._detail_data)

    def _clear_detail(self) -> None:
        self._detail, self._detail_data = {}, None

    # -- running fetches --------------------------------------------------

    async def _run(
        self, fetch: Callable[[], Awaitable[Any]], apply: Callable[[Any], None]
    ) -> None:
        async def once() -> AsyncIterator[Any]:
            yield await fetch()

        await self._run_stream(once, apply)

    async def _run_stream(
        self,
        stream: Callable[[], AsyncIterator[Any]],
        apply: Callable[[Any], None],
    ) -> None:
        """Apply each value the stream yields; only a failure before the first
        value is shown to the user, later ones are background refreshes."""
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
        except _FAILURES as exc:
            if generation == self._generation:
                if shown:
                    logger.warning(
                        "[workshop] background refresh failed: {}", _message(exc)
                    )
                else:
                    self._error = _message(exc)
        finally:
            if generation == self._generation:
                self._busy = False
                self.changed.emit()

    async def _run_cached(
        self,
        key: str,
        produce: Callable[[], Awaitable[Any]],
        value_type: Any,
        apply: Callable[[Any], None],
        *,
        fresh_for: float = FRESH_SECONDS,
    ) -> None:
        await self._run_stream(
            lambda: self._catalog.cached(
                key, produce, fresh_for=fresh_for, value_type=value_type
            ),
            apply,
        )

    # -- loading tabs -----------------------------------------------------

    async def load(self, *, append: bool = False) -> None:
        if not self._catalog.configured:
            self._generation += 1
            self._busy = False
            self.changed.emit()
            return
        self._clear_detail()
        match self._tab:
            case "Discover":
                await self._load_discover()
            case "Installed":
                await self._load_installed(append)
            case _:
                await self._load_listing(append)

    async def _load_discover(self) -> None:
        def apply(result: Discover) -> None:
            self.mods_model.set_page(
                self._rows(result.mods.items), result.mods.total, None
            )
            self.collections_model.set_page(
                self._rows(result.collections.items), result.collections.total, None
            )

        await self._run_cached("discover", self._catalog.discover, Discover, apply)

    async def _load_listing(self, append: bool) -> None:
        collections = self._tab == "Collections"
        model = self.collections_model if collections else self.mods_model
        fetch = self._catalog.collections if collections else self._catalog.mods
        query = msgspec.structs.replace(
            self._criteria, cursor=model.cursor if append else None
        )

        def apply(page: CatalogPage[Any]) -> None:
            model.set_page(
                self._rows(page.items), page.total, page.next_cursor, append=append
            )

        if append:
            await self._run(lambda: fetch(query), apply)
            return
        page_type = (
            CatalogPage[CatalogCollection] if collections else CatalogPage[CatalogMod]
        )
        await self._run_cached(
            f"{self._tab}|{query!r}", lambda: fetch(query), page_type, apply
        )

    async def _load_installed(self, append: bool) -> None:
        if append:
            self.mods_model.set_page(
                self._rows(self._installed.next_page()),
                self._installed.total,
                self._installed_cursor(),
                append=True,
            )
            return

        def apply(mods: list[CatalogMod]) -> None:
            self._installed.replace(mods)
            self._recount()
            self._show_installed()

        await self._run_cached(
            "installed",
            self._catalog.installed_with_updates,
            list[CatalogMod],
            apply,
            fresh_for=0,
        )

    def _installed_cursor(self) -> str | None:
        return "more" if self._installed.has_more else None

    def _show_installed(self) -> None:
        self._clear_detail()
        self.mods_model.set_page(
            self._rows(self._installed.visible()),
            self._installed.total,
            self._installed_cursor(),
        )
        self.changed.emit()

    def _recount(self) -> None:
        self._update_count = sum(
            self._catalog.install_state(mod) == "outdated"
            for mod in self._installed.mods
        )

    # -- QML slots --------------------------------------------------------

    @asyncSlot()
    async def refresh(self) -> None:
        await self.load()

    @asyncSlot(str)
    async def selectTab(self, tab: str) -> None:
        self._tab, self._notice, self._pending = tab, "", None
        self._installed.reset_paging()
        await self.load()

    @asyncSlot(str, str, str, str, str)
    async def filter(
        self, query: str, version: str, source: str, sort: str, tag: str
    ) -> None:
        self._criteria = CatalogQuery(
            query=query,
            tag=tag or None,
            version=version or None,
            sort=sort,
            source=source,
        )
        self._installed.search(query)
        if self._tab == "Installed" and self._installed.loaded:
            self._show_installed()
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
        def apply(detail: Detail) -> None:
            members = self._rows(detail.members)
            self._detail_data = detail
            self._detail = self._detail_row(detail)
            self.members_model.set_page(members, len(members), None)

        await self._run_cached(
            f"detail|{kind}|{item_id}",
            lambda: fetch_detail(self._catalog, item_id, kind),
            Detail,
            apply,
        )

    @Slot()
    def back(self) -> None:
        self._generation += 1
        self._clear_detail()
        self._error, self._busy = "", False
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
            mod.id
            for mod in self._installed.mods
            if self._catalog.install_state(mod) == "outdated"
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

    # -- downloads --------------------------------------------------------

    def _queue_plan(self, plan: DownloadPlan) -> None:
        self._catalog.enqueue(plan)
        self._notice = queued_notice(len(plan.to_download))

    async def _download(self, ids: list[str], collections: list[str]) -> None:
        self._notice, self._pending = "Preparing download…", None
        self._planning += 1
        self.changed.emit()
        try:
            plan = await self._catalog.plan(ids, collections)
        except _FAILURES as exc:
            self._notice = f"Could not prepare the download: {_message(exc)}"
            return
        finally:
            self._planning -= 1
            self.changed.emit()
        if not plan.is_complete:
            self._pending, self._notice = plan, incomplete_plan_notice(plan)
        elif not plan.to_download:
            self._notice = "All available mods are already installed and up to date."
        else:
            self._queue_plan(plan)
        self.changed.emit()

    # -- catalog events ---------------------------------------------------

    def _on_queue_changed(self, *_: object) -> None:
        self._refresh_rows()
        self.changed.emit()

    @asyncSlot(str)
    async def _on_url_changed(self, _url: str) -> None:
        self._clear_detail()
        self._error, self._notice, self._pending = "", "", None
        self._items.clear()
        for model in (self.mods_model, self.collections_model, self.members_model):
            model.set_page([], 0, None)
        await self.load()

    @asyncSlot(object)
    async def _on_installed_changed(self, _value: None) -> None:
        self._recount()
        self._refresh_rows()
        self.changed.emit()
        if self._tab == "Installed" and not self._busy:
            await self.load()

    # -- QML properties ---------------------------------------------------

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
        return self._game_version

    @Property(list, constant=True)  # type: ignore[arg-type]
    def versionOptions(self) -> list[str]:
        current = self._criteria.version
        return list(
            dict.fromkeys(
                [
                    "All versions",
                    *([current] if current else []),
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
