from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator, Awaitable, Callable, Iterable
from hashlib import sha256
from pathlib import Path
from typing import TYPE_CHECKING, Any, cast

import msgspec
from loguru import logger
from PySide6.QtCore import Property, QUrl, Signal, Slot
from PySide6.QtGui import QColor, QDesktopServices
from PySide6.QtQuickWidgets import QQuickWidget
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
from pxmodrim.ui.components.mod_activation import apply_activation
from pxmodrim.ui.navigation import SETTINGS_VIEW_ID, build_route
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
_TABS = ("Discover", "Mods", "Collections", "Installed")


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
        self._torn_down = False
        self._retry: Callable[[], Awaitable[None]] | None = None
        self._detail: dict[str, Any] = {}
        self._detail_data: Detail | None = None
        self._installed = InstalledList()
        self._installed_dirty = False
        self._route_tasks: set[asyncio.Future[None]] = set()
        self._current: tuple[str, str] | None = None
        self._trail: list[tuple[str, str, str]] = []
        self._update_count = 0
        self._pending: DownloadPlan | None = None
        self._planning = 0
        self._items: dict[str, CatalogMod | CatalogCollection] = {}
        self._known_tags: set[str] = set()
        self._active_ids: set[str] = set()
        self._pack_ids: frozenset[str] = frozenset()
        self._refresh_active_ids()
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
        self._ctx.active_state_changed.connect(self._on_active_state_changed)
        self.destroyed.connect(lambda: self.teardown())

    def teardown(self) -> None:
        if self._torn_down:
            return
        self._torn_down = True
        self._generation += 1
        self._retry = None
        self._downloads.busy_changed.disconnect(self._on_queue_changed)
        self._catalog.queue_changed.disconnect(self._on_queue_changed)
        self._catalog.catalog_url_changed.disconnect(self._on_url_changed)
        self._catalog.installed_changed.disconnect(self._on_installed_changed)
        self._ctx.active_state_changed.disconnect(self._on_active_state_changed)

    @property
    def _game_version(self) -> str:
        version = self._ctx.target_version
        return "" if version == "Unknown" else version

    # -- rows -------------------------------------------------------------

    def _refresh_active_ids(self) -> None:
        mods = self._ctx.all_mods
        self._active_ids = {
            published_id
            for uuid in self._ctx.active_uuids
            if uuid in mods
            and (published_id := mods[uuid].published_file_id) is not None
        }

    def _row(
        self, item: CatalogMod | CatalogCollection, *, detail: bool = False
    ) -> dict[str, Any]:
        self._items[item.id] = item
        self._known_tags.update(set(item.tags) - set(item.supported_versions))
        row = item_row(
            item, self._catalog.install_state, self._game_version, detail=detail
        )
        row["active"] = isinstance(item, CatalogMod) and item.id in self._active_ids
        if isinstance(item, CatalogMod) and row["state"] != "installed":
            running = item.id in self._downloads.active_ids
            if running or item.id in self._catalog.queued_ids:
                row["queued"] = True
                row["actionLabel"] = "Downloading" if running else "Queued"
        return row

    def _rows(self, items: Iterable[CatalogMod | CatalogCollection]) -> list[Any]:
        return [self._row(item) for item in items]

    def _detail_row(self, detail: Detail) -> dict[str, Any]:
        row = self._row(detail.item, detail=True)
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
            copies = self._pack_copies()
            if copies is not None:
                active = set(self._ctx.active_uuids)
                row["state"] = "installed"
                row["actionLabel"] = "Installed"
                row["active"] = all(uuid in active for uuid in copies)
        return row

    def _refresh_rows(self) -> None:
        for model in (self.mods_model, self.collections_model, self.members_model):
            model.refresh_rows(lambda row: self._row(self._items[row["itemId"]]))
        if self._detail_data is not None:
            self._detail = self._detail_row(self._detail_data)

    def _clear_detail(self) -> None:
        self._detail, self._detail_data = {}, None
        self._current = None
        self._trail.clear()
        self._pack_ids = frozenset()

    def _pack_copies(self) -> list[str] | None:
        """One installed copy per collection mod, or None until all are downloaded."""
        if self._torn_down or not self._pack_ids:
            return None
        installed: dict[str, list[str]] = {}
        for uuid, mod in self._ctx.all_mods.items():
            published_id = mod.published_file_id
            if published_id is not None and published_id in self._pack_ids:
                installed.setdefault(published_id, []).append(uuid)
        if len(installed) < len(self._pack_ids):
            return None
        active = set(self._ctx.active_uuids)
        return [
            next((uuid for uuid in uuids if uuid in active), uuids[0])
            for uuids in installed.values()
        ]

    async def _load_pack(self) -> None:
        """Resolve the collection's mods and dependencies for the activation buttons."""
        detail, generation = self._detail_data, self._generation
        if detail is None:
            return
        try:
            plan = await self._catalog.plan([], [detail.item.id])
        except _FAILURES as exc:
            logger.warning(
                "[workshop] cannot resolve collection mods: {}", _message(exc)
            )
            return
        if generation != self._generation:
            return
        self._pack_ids = (
            frozenset((*plan.to_download, *plan.already_current))
            if plan.is_complete
            else frozenset()
        )
        self._refresh_rows()
        self.changed.emit()

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
        if self._torn_down:
            return
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
                if (
                    self._installed_dirty
                    and self._tab == "Installed"
                    and self._detail_data is None
                ):
                    await self.load()

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
        if self._torn_down:
            return
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

        self._installed_dirty = False

        def apply(mods: list[CatalogMod]) -> None:
            self._installed.replace(mods)
            self._recount()
            self._show_installed()

        installed_ids = {
            mod.published_file_id
            for mod in self._ctx.all_mods.values()
            if mod.published_file_id is not None
        }
        fingerprint = sha256(",".join(sorted(installed_ids)).encode()).hexdigest()

        await self._run_cached(
            f"installed|{fingerprint}",
            self._catalog.installed_with_updates,
            list[CatalogMod],
            apply,
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

    @Slot(str)
    def selectTab(self, tab: str) -> None:
        self._go(tab.lower())

    async def select_tab(self, tab: str) -> None:
        self._tab, self._notice, self._pending = tab, "", None
        self._clear_detail()
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

    @Slot(str, str)
    def openItem(self, item_id: str, kind: str) -> None:
        self._go(kind, item_id)

    def _go(self, *path: str) -> None:
        if self._app_ctx is None:
            self.open_route(path)
        else:
            self._app_ctx.navigate(build_route(self.view_id, *path))

    async def open_item(self, item_id: str, kind: str, *, record: bool = True) -> None:
        self._pack_ids = frozenset()

        def apply(detail: Detail) -> None:
            ref = (kind, item_id)
            if self._current != ref:
                if not self._detail:
                    self._trail.clear()
                elif record and self._current is not None:
                    title = str(self._detail.get("title", ""))
                    self._trail.append((*self._current, title))
            self._current = ref
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
        if kind == "collection":
            await self._load_pack()

    def open_route(self, path: tuple[str, ...]) -> None:
        task = asyncio.ensure_future(self.follow_route(path))
        self._route_tasks.add(task)
        task.add_done_callback(self._route_tasks.discard)

    async def follow_route(self, path: tuple[str, ...]) -> None:
        if self._torn_down:
            return
        if len(path) == 1 and path[0].title() in _TABS:
            await self.select_tab(path[0].title())
        elif len(path) == 2 and path[0] in ("mod", "collection"):
            await self.open_item(path[1], path[0])
        else:
            logger.warning(
                "[workshop] unknown route {}", build_route(self.view_id, *path)
            )

    @asyncSlot()
    async def back(self) -> None:
        if self._torn_down:
            return
        if self._trail:
            kind, item_id, _ = self._trail[-1]
            await self.open_item(item_id, kind, record=False)
            if self._current == (kind, item_id) and self._trail:
                self._trail.pop()
                self.changed.emit()
            return
        self._generation += 1
        self._clear_detail()
        self._error, self._busy = "", False
        self.changed.emit()
        if self._tab == "Installed" and self._installed_dirty:
            await self.load()

    @asyncSlot(str, str)
    async def downloadItem(self, item_id: str, kind: str) -> None:
        await self._download(
            [item_id] if kind == "mod" else [],
            [item_id] if kind == "collection" else [],
        )

    @asyncSlot(str)
    async def toggleActivation(self, item_id: str) -> None:
        if self._torn_down:
            return
        installed = [
            uuid
            for uuid, mod in self._ctx.all_mods.items()
            if mod.published_file_id == item_id
        ]
        if not installed:
            return
        active = set(self._ctx.active_uuids)
        active_copies = [uuid for uuid in installed if uuid in active]
        if active_copies:
            await apply_activation(self._ctx, self, disable=active_copies)
        else:
            await apply_activation(self._ctx, self, enable=[installed[0]])

    @asyncSlot()
    async def toggleCollection(self) -> None:
        copies = self._pack_copies()
        if copies is None:
            return
        active = set(self._ctx.active_uuids)
        if all(uuid in active for uuid in copies):
            await apply_activation(
                self._ctx,
                self,
                disable=[
                    uuid
                    for uuid in self._ctx.active_uuids
                    if (mod := self._ctx.all_mods.get(uuid)) is not None
                    and mod.published_file_id in self._pack_ids
                ],
            )
        else:
            await apply_activation(self._ctx, self, enable=copies)

    @asyncSlot()
    async def activateOnlyCollection(self) -> None:
        copies = self._pack_copies()
        if copies is None:
            return
        keep = set(copies)
        mods = self._ctx.all_mods
        others = [
            uuid
            for uuid in self._ctx.active_uuids
            if uuid not in keep
            and (mod := mods.get(uuid)) is not None
            and mod.published_file_id is not None
        ]
        await apply_activation(self._ctx, self, enable=copies, disable=others)

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
        if self._torn_down:
            return
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

    @Slot()
    def openSettings(self) -> None:
        if self._app_ctx is not None:
            self._app_ctx.navigate(build_route(SETTINGS_VIEW_ID))

    # -- downloads --------------------------------------------------------

    def _queue_plan(self, plan: DownloadPlan) -> None:
        self._catalog.enqueue(plan)
        self._notice = queued_notice(len(plan.to_download))

    async def _download(self, ids: list[str], collections: list[str]) -> None:
        if self._torn_down:
            return
        self._notice, self._pending = "Preparing download…", None
        self._planning += 1
        self.changed.emit()
        try:
            plan = await self._catalog.plan(ids, collections)
        except _FAILURES as exc:
            if not self._torn_down:
                self._notice = f"Could not prepare the download: {_message(exc)}"
            return
        finally:
            self._planning -= 1
            if not self._torn_down:
                self.changed.emit()
        if self._torn_down:
            return
        if not plan.is_complete:
            self._pending, self._notice = plan, incomplete_plan_notice(plan)
        elif not plan.to_download:
            self._notice = "All available mods are already installed and up to date."
        else:
            self._queue_plan(plan)
        self.changed.emit()

    # -- catalog events ---------------------------------------------------

    def _on_active_state_changed(self, _uuids: tuple[str, ...]) -> None:
        self._refresh_active_ids()
        self._refresh_rows()
        self.changed.emit()

    def _on_queue_changed(self, *_: object) -> None:
        self._refresh_rows()
        self.changed.emit()

    @asyncSlot(str)
    async def _on_url_changed(self, _url: str) -> None:
        if self._torn_down:
            return
        self._clear_detail()
        self._error, self._notice, self._pending = "", "", None
        self._items.clear()
        self._known_tags.clear()
        for model in (self.mods_model, self.collections_model, self.members_model):
            model.set_page([], 0, None)
        await self.load()

    @asyncSlot(object)
    async def _on_installed_changed(self, _value: None) -> None:
        if self._torn_down:
            return
        self._installed_dirty = True
        self._refresh_active_ids()
        self._recount()
        self._refresh_rows()
        self.changed.emit()
        if self._tab == "Installed" and not self._busy and self._detail_data is None:
            await self.load()

    # -- QML properties ---------------------------------------------------

    @Property(str, notify=changed)  # type: ignore[arg-type]
    def tab(self) -> str:
        return self._tab

    @Property(str, notify=changed)  # type: ignore[arg-type]
    def url(self) -> str:
        if self._current is not None:
            return build_route(self.view_id, *self._current)
        return build_route(self.view_id, self._tab.lower())

    @Property(str, notify=changed)  # type: ignore[arg-type]
    def backLabel(self) -> str:
        if self._trail:
            return f"Back to {self._trail[-1][2]}"
        return f"Back to {self._tab}"

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

    @Property(str, notify=changed)  # type: ignore[arg-type]
    def searchQuery(self) -> str:
        return self._criteria.query

    @Property(list, notify=changed)  # type: ignore[arg-type]
    def tagOptions(self) -> list[str]:
        return ["All tags", *sorted(self._known_tags)]

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
