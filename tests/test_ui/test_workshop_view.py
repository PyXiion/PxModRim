from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator
from pathlib import Path
from types import SimpleNamespace
from typing import Any, cast

import msgspec
import pytest
from PySide6.QtCore import QObject, QPoint, Qt, QUrl
from PySide6.QtGui import QAccessible, QKeySequence
from PySide6.QtQuick import QQuickItem
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QWidget
from pytestqt.qtbot import QtBot
from shiboken6 import delete, isValid

from pxmodrim.core.config import AppConfig, ConfigService
from pxmodrim.core.context import CoreContext
from pxmodrim.core.downloads import DownloadManager
from pxmodrim.core.models.metadata.structures import (
    AboutXmlMod,
    BaseRules,
    CaseInsensitiveStr,
    DependencyMod,
    ListedMod,
)
from pxmodrim.core.workshop import (
    Author,
    CatalogCollection,
    CatalogError,
    CatalogMod,
    CatalogPage,
    CatalogQuery,
    CollectionDetail,
    Discover,
    DownloadPlan,
    InstallState,
    Preview,
    WorkshopCatalog,
)
from pxmodrim.ui.components import create_qml_engine
from pxmodrim.ui.context import AppContext
from pxmodrim.ui.navigation import parse_route
from pxmodrim.ui.panels.mod_info_data import description_to_html
from pxmodrim.ui.plugins.workshop.plugin import CatalogSettingsSection
from pxmodrim.ui.plugins.workshop.view import WorkshopViewPanel
from pxmodrim.ui.theme.qml_theme import Theme


def _mod(pid: str, dependencies: list[str] | None = None) -> CatalogMod:
    return CatalogMod(
        id=pid,
        source="steam",
        title="<b>Untrusted title</b>",
        author=Author("1", "Author", None),
        description="[b]Safe description[/b]",
        description_format="bbcode",
        preview_url=None,
        previews=[],
        workshop_url="https://steamcommunity.com/sharedfiles/filedetails/?id=" + pid,
        tags=["Utility"],
        supported_versions=["1.5"],
        created_at=None,
        updated_at=100,
        file_size="1024",
        subscriptions=None,
        votes=None,
        dependencies=dependencies or [],
        package_id=None,
        incompatible=False,
    )


class FakeCatalog(WorkshopCatalog):
    def __init__(self, ctx: CoreContext) -> None:
        super().__init__()
        self.mod_item = _mod("1", ["2"])
        self.dependency = _mod("2")
        self.collection_item = CatalogCollection(
            id="picked:test",
            source="picked",
            steam_id=None,
            title="Test collection",
            author=Author(None, "Curator", None),
            description="<b>Plain text</b>",
            description_format="text",
            preview_url=None,
            previews=[],
            workshop_url=None,
            tags=[],
            supported_versions=["1.6"],
            created_at=None,
            updated_at=None,
            member_ids=["1", "missing"],
            member_count=2,
        )
        self.failure = False
        self.collection_complete = False
        self.downloaded: list[str] = []
        self.queries: list[CatalogQuery] = []

    async def discover(self) -> Discover:
        return Discover(
            CatalogPage([self.mod_item], 1, None),
            CatalogPage([self.collection_item], 1, None),
        )

    async def mods(self, q: CatalogQuery) -> CatalogPage[CatalogMod]:
        self.queries.append(q)
        if self.failure:
            raise CatalogError("Catalog unavailable", 503)
        return CatalogPage(
            [self.dependency] if q.cursor else [self.mod_item],
            2,
            None if q.cursor else "second",
        )

    async def collections(self, q: CatalogQuery) -> CatalogPage[CatalogCollection]:
        return CatalogPage([self.collection_item], 1, None)

    async def mod(self, id: str) -> CatalogMod | None:
        return {"1": self.mod_item, "2": self.dependency}.get(id)

    async def collection(self, id: str) -> CollectionDetail | None:
        if self.collection_complete:
            collection = msgspec.structs.replace(
                self.collection_item, member_ids=["1"], member_count=1
            )
            return CollectionDetail(collection, [self.mod_item], [], True)
        return CollectionDetail(
            self.collection_item, [self.mod_item], ["missing"], False
        )

    async def installed_with_updates(self) -> list[CatalogMod]:
        return [self.mod_item]

    def install_state(self, mod: CatalogMod) -> InstallState:
        return "outdated" if mod.id == "1" else "missing"

    async def plan(self, mod_ids: list[str], collection_ids: list[str]) -> DownloadPlan:
        incomplete = bool(collection_ids) and not self.collection_complete
        return DownloadPlan(
            ["2", "1"],
            [],
            ["missing"] if incomplete else [],
            collection_ids if incomplete else [],
            {"1": "Mod", "2": "Dependency"},
            not incomplete,
        )

    def enqueue(self, plan: DownloadPlan) -> None:
        self.downloaded.extend(plan.to_download)


@pytest.fixture
async def panel(
    qapp: object, config_service: ConfigService
) -> AsyncIterator[tuple[WorkshopViewPanel, FakeCatalog, list[Any]]]:
    owner = QWidget()
    ctx = CoreContext.create(AppConfig(), config_service)
    manager = DownloadManager()
    ctx.plugins.register(manager)
    catalog = FakeCatalog(ctx)
    engine = create_qml_engine()
    engine.setParent(owner)
    theme = Theme(engine)
    engine.rootContext().setContextProperty("Theme", theme)
    warnings: list[Any] = []

    def _on_warning(qml_warnings: list[Any]) -> None:
        for w in qml_warnings:
            msg = w.toString() if hasattr(w, "toString") else str(w)
            if "The current style does not support customization" in msg:
                continue
            warnings.append(w)

    engine.warnings.connect(_on_warning)
    view = WorkshopViewPanel(ctx, engine, owner, catalog=catalog)
    view.resize(1100, 800)
    yield view, catalog, warnings
    if isValid(view):
        view.teardown()
        view._qml.setSource(QUrl())
    owner.deleteLater()


@pytest.fixture
def installed_mods(tmp_path: Path) -> dict[str, ListedMod]:
    mods: dict[str, ListedMod] = {}
    for provider, pid in [("steamcmd", "1"), ("steam", "1"), ("steam", "2")]:
        mod = AboutXmlMod(
            name=f"Installed {pid}",
            package_id=CaseInsensitiveStr(f"test.mod{pid}"),
            provider_id=provider,
            _mod_path=tmp_path / provider / pid,
        )
        mods[mod.uuid] = mod
    return mods


async def test_view_loads_qml_and_discovery_without_errors(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    view, _, warnings = panel
    await asyncio.sleep(0)
    assert view._qml.status() == QQuickWidget.Status.Ready
    assert view.mods_model.count == 1 and view.collections_model.count == 1
    assert not warnings, [warning.toString() for warning in warnings]


async def test_destroyed_view_does_not_receive_core_shutdown_events(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    view, catalog, _ = panel
    await view.load()
    model = view.mods_model
    assert model.count == 1
    notifications: list[None] = []
    catalog.queue_changed.connect(notifications.append)
    downloads = view._downloads
    ctx = view._ctx
    view._qml.setSource(QUrl())
    delete(view)
    await catalog.shutdown()
    downloads.busy_changed.emit(False)
    catalog.installed_changed.emit(None)
    catalog.catalog_url_changed.emit("")
    ctx.active_state_changed.emit(())
    assert not isValid(model)
    assert notifications == [None]


async def test_teardown_discards_load_finishing_after_view_destruction(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    view, catalog, _ = panel
    await view.load()
    started, release = asyncio.Event(), asyncio.Event()
    discover = catalog.discover

    async def delayed_discover() -> Discover:
        started.set()
        await release.wait()
        return await discover()

    monkeypatch.setattr(catalog, "discover", delayed_discover)
    catalog._responses.clear()
    task = asyncio.ensure_future(view.refresh())
    async with asyncio.timeout(3):
        await started.wait()
    view.teardown()
    view.teardown()
    model = view.mods_model
    view._qml.setSource(QUrl())
    delete(view)
    release.set()
    await task
    assert not isValid(model)


async def test_view_paging_filters_and_catalog_errors(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    view, catalog, _ = panel
    await asyncio.sleep(0)
    await view.select_tab("Mods")
    await view.filter("", "1.6", "all", "popular", "")
    await view.loadMore()
    assert view.mods_model.count == 2 and not view.mods_model.hasMore
    assert (
        catalog.queries[-1].cursor == "second" and catalog.queries[-1].version == "1.6"
    )
    catalog.failure = True
    await view.filter("uncached", "1.6", "all", "popular", "")
    assert view.error == "Catalog unavailable"
    catalog.failure = False
    await view.retry()
    assert view.error == "" and view.mods_model.count == 1


async def test_mod_detail_safe_description_dependencies_and_download(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    view, catalog, warnings = panel
    await asyncio.sleep(0)
    await view.open_item("1", "mod")
    detail = cast("dict[str, Any]", view.detail)
    assert detail["description"] == description_to_html(
        catalog.mod_item.description, catalog.mod_item.description_format
    )
    assert detail["actionLabel"] == "Update"
    assert view.members_model.count == 1
    await view.downloadItem("1", "mod")
    assert catalog.downloaded == ["2", "1"]
    assert view._ctx.active_uuids == []
    assert not warnings, [warning.toString() for warning in warnings]


async def test_incomplete_collection_requires_explicit_partial_download(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    view, catalog, warnings = panel
    await asyncio.sleep(0)
    await view.open_item("picked:test", "collection")
    detail = cast("dict[str, Any]", view.detail)
    assert not detail["complete"] and "missing" in detail["warning"]
    assert detail["description"] == description_to_html(
        catalog.collection_item.description, catalog.collection_item.description_format
    )
    await view.downloadItem("picked:test", "collection")
    assert view.pendingPlan and not catalog.downloaded
    assert view.canDownloadAvailable
    await view.downloadAvailable()
    assert catalog.downloaded == ["2", "1"] and not view.pendingPlan
    assert not warnings, [warning.toString() for warning in warnings]


async def test_collection_detail_uses_member_images_when_preview_metadata_is_absent(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    view, catalog, _ = panel
    catalog.mod_item = msgspec.structs.replace(
        catalog.mod_item,
        preview_url=None,
        previews=[Preview("image", "https://images.test/member.png")],
    )
    await view.open_item("picked:test", "collection")
    detail = cast("dict[str, Any]", view.detail)
    assert detail["collage"] == ["https://images.test/member.png"]


async def test_unset_catalog_never_fetches(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    view, catalog, _ = panel
    await asyncio.sleep(0)
    catalog.set_base_url("")
    await view.load()
    assert not view.configured and not catalog.queries


async def test_settings_saves_and_applies_catalog_url(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    from pxmodrim.ui.panels.settings_panel import SettingsPanel

    view, catalog, warnings = panel
    await asyncio.sleep(0)
    settings = SettingsPanel(
        view._ctx,
        view._qml.engine(),
        view,
        [lambda parent: CatalogSettingsSection(catalog.settings, parent)],
    )
    settings.show()
    await asyncio.sleep(0.2)
    root = settings._qml.rootObject()
    assert root is not None
    field = _find_item(cast("QQuickItem", root), "catalogUrlField")
    assert field is not None
    assert field.property("text") == "https://api.modrim.pyxiion.dev"
    field.forceActiveFocus()
    QTest.keySequence(settings._qml, QKeySequence.StandardKey.SelectAll)
    QTest.keyClicks(settings._qml, " https://catalog.example.test/ ")
    assert catalog.configured and catalog._base_url != "https://catalog.example.test"

    settings._save(dict(cast("dict[str, Any]", settings._backend.initial)))
    assert catalog.settings.value.url == "https://catalog.example.test/"
    assert catalog._base_url == "https://catalog.example.test"
    assert warnings == []
    settings.deleteLater()


async def test_installed_search_filters_loaded_mods_without_refetching(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    view, catalog, _ = panel
    await asyncio.sleep(0)
    fetches = 0
    base = catalog.mod_item
    mods = [
        msgspec.structs.replace(base, id=str(i), title=title)
        for i, title in enumerate(["Harmony", "HugsLib", "Rim HUD"])
    ]

    async def installed() -> list[CatalogMod]:
        nonlocal fetches
        fetches += 1
        return mods

    catalog.installed_with_updates = installed  # type: ignore[method-assign]
    await view.select_tab("Installed")
    assert view.mods_model.count == 3 and fetches == 1

    await view.filter("hugs", "", "all", "popular", "")
    assert view.mods_model.count == 1 and view.tab == "Installed" and fetches == 1
    await view.filter("", "", "all", "popular", "")
    assert view.mods_model.count == 3 and fetches == 1
    assert view.searchQuery == ""


async def test_installed_set_changes_refresh_the_cached_list(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
    installed_mods: dict[str, ListedMod],
    tmp_path: Path,
) -> None:
    view, catalog, _ = panel
    catalog._ctx = view._ctx
    catalog._cache_dir = tmp_path / "cache"
    steamcmd, _, dependency = installed_mods
    view._ctx.load({steamcmd: installed_mods[steamcmd]}, [])
    fetches = 0

    async def installed() -> list[CatalogMod]:
        nonlocal fetches
        fetches += 1
        items = {"1": catalog.mod_item, "2": catalog.dependency}
        return [items[pid] for pid in catalog._installed()]

    catalog.installed_with_updates = installed  # type: ignore[method-assign]
    await view.select_tab("Installed")
    await view.select_tab("Mods")
    await view.select_tab("Installed")
    assert fetches == 1 and view.mods_model.count == 1

    catalog.installed_changed.disconnect(view._on_installed_changed)
    view._ctx.load(installed_mods, [])
    catalog._on_installed_changed(None)
    catalog.installed_changed.connect(view._on_installed_changed)
    await view._on_installed_changed(None)
    assert fetches == 2 and view.mods_model.count == 2
    assert {row["itemId"] for row in view.mods_model._rows} == {"1", "2"}

    catalog.installed_changed.disconnect(view._on_installed_changed)
    view._ctx.load({dependency: installed_mods[dependency]}, [])
    catalog._on_installed_changed(None)
    catalog.installed_changed.connect(view._on_installed_changed)
    await view._on_installed_changed(None)
    assert fetches == 3 and view.mods_model.count == 1
    assert view.mods_model._rows[0]["itemId"] == "2"

    catalog._responses.clear()
    await view.select_tab("Mods")
    await view.select_tab("Installed")
    assert fetches == 3 and view.mods_model._rows[0]["itemId"] == "2"


@pytest.mark.parametrize("state", ["installed", "outdated"])
@pytest.mark.parametrize("duplicate_active", [False, True])
async def test_activation_toggles_one_installed_copy_and_prefers_active_steam_copy(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
    installed_mods: dict[str, ListedMod],
    monkeypatch: pytest.MonkeyPatch,
    state: InstallState,
    duplicate_active: bool,
) -> None:
    view, catalog, _ = panel
    steamcmd, steam, unrelated = installed_mods
    view._ctx.load(installed_mods, [])
    view._ctx.set_active(
        [steamcmd, steam, unrelated] if duplicate_active else [steam, unrelated]
    )
    monkeypatch.setattr(catalog, "install_state", lambda _: state)
    await view.load()
    await view.open_item("1", "mod")
    role = next(
        role
        for role, name in view.mods_model.roleNames().items()
        if bytes(name.data()) == b"active"
    )
    assert view.mods_model.data(view.mods_model.index(0), role) is True
    assert cast("dict[str, Any]", view.detail)["active"] is True
    expected_action = "Update" if state == "outdated" else "Installed"
    assert cast("dict[str, Any]", view.detail)["actionLabel"] == expected_action

    await view.toggleActivation("1")
    assert view._ctx.active_uuids == [unrelated]
    assert view.mods_model.data(view.mods_model.index(0), role) is False
    assert cast("dict[str, Any]", view.detail)["active"] is False

    await view.toggleActivation("1")
    assert set(view._ctx.active_uuids) == {steamcmd, unrelated}
    assert steam not in view._ctx.active_uuids
    assert view.mods_model.data(view.mods_model.index(0), role) is True
    assert cast("dict[str, Any]", view.detail)["active"] is True


async def test_external_activation_refreshes_listing_detail_and_members(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
    installed_mods: dict[str, ListedMod],
) -> None:
    view, _, _ = panel
    _, steam, dependency = installed_mods
    view._ctx.load(installed_mods, [])
    await view.load()
    await view.open_item("1", "mod")
    role = next(
        role
        for role, name in view.mods_model.roleNames().items()
        if bytes(name.data()) == b"active"
    )

    for active, expected in [([steam, dependency], True), ([], False)]:
        view._ctx.set_active(active)
        assert view.mods_model.data(view.mods_model.index(0), role) is expected
        assert view.members_model.data(view.members_model.index(0), role) is expected
        assert cast("dict[str, Any]", view.detail)["active"] is expected


async def test_download_never_activates_installed_copies(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
    installed_mods: dict[str, ListedMod],
) -> None:
    view, catalog, _ = panel
    view._ctx.load(installed_mods, [])
    await view.downloadItem("1", "mod")
    assert catalog.downloaded == ["2", "1"]
    assert view._ctx.active_uuids == []


def _find_item(item: QQuickItem, name: str) -> QQuickItem | None:
    if item.objectName() == name:
        return item
    for child in item.childItems():
        found = _find_item(child, name)
        if found is not None:
            return found
    return None


async def test_installed_checkbox_click_toggles_activation_without_opening_details(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
    installed_mods: dict[str, ListedMod],
) -> None:
    view, catalog, _ = panel
    catalog._ctx = view._ctx
    steamcmd, _, _ = installed_mods
    view._ctx.load({steamcmd: installed_mods[steamcmd]}, [])
    view.window().resize(1100, 800)
    view.window().show()
    await view.select_tab("Installed")
    await asyncio.sleep(0.5)
    view._qml.grabFramebuffer()
    await asyncio.sleep(0.2)

    root = view._qml.rootObject()
    assert root is not None
    box = _find_item(cast("QQuickItem", root), "activeToggle")
    assert box is not None
    center = box.mapToScene(box.boundingRect().center()).toPoint()
    QTest.mouseClick(view._qml, Qt.MouseButton.LeftButton, pos=center)
    async with asyncio.timeout(3):
        while not view._ctx.active_uuids:
            await asyncio.sleep(0.02)
    assert view._ctx.active_uuids == [steamcmd]
    assert not view.hasDetail


async def test_clicking_an_installed_row_navigates_by_link(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
    installed_mods: dict[str, ListedMod],
) -> None:
    view, catalog, _ = panel
    catalog._ctx = view._ctx
    steamcmd, _, _ = installed_mods
    view._ctx.load({steamcmd: installed_mods[steamcmd]}, [])
    links: list[str] = []

    def navigate(url: str) -> None:
        links.append(url)
        route = parse_route(url)
        assert route is not None
        view.open_route(route.path)

    view._app_ctx = cast("AppContext", SimpleNamespace(navigate=navigate))
    view.window().resize(1100, 800)
    view.window().show()
    await view.select_tab("Installed")
    await asyncio.sleep(0.5)
    view._qml.grabFramebuffer()
    await asyncio.sleep(0.2)

    root = view._qml.rootObject()
    assert root is not None
    box = _find_item(cast("QQuickItem", root), "activeToggle")
    assert box is not None
    beside = box.mapToScene(box.boundingRect().center()).toPoint()
    beside.setX(beside.x() + 150)
    QTest.mouseClick(view._qml, Qt.MouseButton.LeftButton, pos=beside)
    async with asyncio.timeout(3):
        while not view.hasDetail:
            await asyncio.sleep(0.02)
    assert links == ["modrim://workshop/mod/1"]
    assert view.url == "modrim://workshop/mod/1"
    assert not view._ctx.active_uuids


async def test_activation_does_nothing_without_an_installed_copy(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    view, catalog, _ = panel
    await view.toggleActivation("1")
    assert view._ctx.active_uuids == []
    assert catalog.downloaded == []


async def test_teardown_disconnects_activation_and_blocks_toggles(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
    installed_mods: dict[str, ListedMod],
) -> None:
    view, _, _ = panel
    _, steam, _ = installed_mods
    view._ctx.load(installed_mods, [])
    await view.load()
    await view.open_item("1", "mod")
    notifications: list[bool] = []
    view.changed.connect(lambda: notifications.append(True))
    view.teardown()
    view.teardown()
    view._ctx.set_active([steam])
    await view.toggleActivation("1")
    assert view._ctx.active_uuids == [steam]
    assert not notifications
    assert cast("dict[str, Any]", view.detail)["active"] is False


async def test_known_tag_options_accumulate_loaded_tags_but_not_version_tags(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    view, catalog, _ = panel
    await asyncio.sleep(0)
    catalog.mod_item = msgspec.structs.replace(
        catalog.mod_item, tags=["Utility", "1.5"]
    )
    catalog.collection_item = msgspec.structs.replace(
        catalog.collection_item, tags=["Curated", "1.6"]
    )
    catalog.dependency = msgspec.structs.replace(
        catalog.dependency, tags=["Dependency", "1.5"]
    )
    catalog._responses.clear()
    await view.load()
    assert view.tagOptions == ["All tags", "Curated", "Utility"]
    await view.filter("utility", "1.5", "all", "popular", "Utility")
    assert view.searchQuery == "utility"
    assert view.tagOptions == ["All tags", "Curated", "Utility"]
    await view.open_item("1", "mod")
    assert view.tagOptions == ["All tags", "Curated", "Dependency", "Utility"]
    await view.select_tab("Installed")
    assert view.searchQuery == "utility"


async def test_back_walks_detail_history_before_returning_to_listing(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    view, catalog, _ = panel
    await view.select_tab("Mods")
    await view.open_item("1", "mod")
    await view.open_item("2", "mod")
    assert view.url == "modrim://workshop/mod/2"
    assert view.backLabel == f"Back to {catalog.mod_item.title}"

    await view.back()
    assert cast("dict[str, Any]", view.detail)["itemId"] == "1"
    assert view.url == "modrim://workshop/mod/1"
    assert view.backLabel == "Back to Mods"

    await view.back()
    assert not view.hasDetail
    assert view.url == "modrim://workshop/mods"


async def test_failed_back_keeps_the_previous_page_for_retry(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    view, catalog, _ = panel
    await view.open_item("1", "mod")
    await view.open_item("2", "mod")
    catalog._responses.clear()
    working = catalog.mod

    async def unavailable(id: str) -> CatalogMod | None:
        raise CatalogError("Catalog unavailable", 503)

    monkeypatch.setattr(catalog, "mod", unavailable)
    await view.back()
    assert view.url == "modrim://workshop/mod/2"
    assert view.backLabel == f"Back to {catalog.mod_item.title}"

    monkeypatch.setattr(catalog, "mod", working)
    await view.back()
    assert view.url == "modrim://workshop/mod/1"


async def test_routes_open_tabs_and_items_like_links(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    view, _, _ = panel
    await view.open_item("1", "mod")
    await view.open_item("2", "mod")

    await view.follow_route(("collection", "picked:test"))
    assert view.url == "modrim://workshop/collection/picked%3Atest"
    await view.back()
    assert view.url == "modrim://workshop/mod/2"

    await view.follow_route(("installed",))
    assert view.tab == "Installed"
    assert view.url == "modrim://workshop/installed"

    await view.follow_route(())
    assert view.url == "modrim://workshop/installed"


async def test_back_preserves_scroll_position_and_active_filters(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    view, catalog, _ = panel
    many = [
        msgspec.structs.replace(catalog.mod_item, id=str(number))
        for number in range(40)
    ]

    async def long_listing(query: CatalogQuery) -> CatalogPage[CatalogMod]:
        return CatalogPage(many, len(many), None)

    catalog.mods = long_listing  # type: ignore[method-assign]
    view.window().resize(800, 600)
    view.window().show()
    await view.load()
    await asyncio.sleep(0.2)
    view._qml.grab()
    await view.filter("test search", "1.5", "all", "popular", "Utility")
    await asyncio.sleep(0.2)
    view._qml.grab()
    assert view.searchQuery == "test search"

    root = view._qml.rootObject()
    assert root is not None
    listing_scroll = root.findChild(QObject, "listingScroll")
    assert listing_scroll is not None
    listing_scroll.setProperty("width", 800)
    listing_scroll.setProperty("height", 600)
    listing_scroll.setProperty("contentY", 184)
    view._qml.grab()
    assert listing_scroll.property("contentY") == 184

    await view.open_item("1", "mod")
    assert view.hasDetail

    await view.back()
    await asyncio.sleep(0.2)
    view._qml.grab()
    assert not view.hasDetail
    assert view.tab == "Mods"
    assert view.searchQuery == "test search"
    assert listing_scroll.property("contentY") == 184


async def test_downloaded_collection_activates_all_or_only_its_pack(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
    installed_mods: dict[str, ListedMod],
    tmp_path: Path,
) -> None:
    view, catalog, _ = panel
    catalog.collection_complete = True
    steamcmd, _, dependency = installed_mods
    outside = AboutXmlMod(
        name="Outside",
        package_id=CaseInsensitiveStr("test.outside"),
        provider_id="steam",
        _mod_path=tmp_path / "steam" / "9",
    )
    local = AboutXmlMod(
        name="Local",
        package_id=CaseInsensitiveStr("test.local"),
        provider_id="local",
        _mod_path=tmp_path / "local" / "Mine",
    )
    view._ctx.load({**installed_mods, outside.uuid: outside, local.uuid: local}, [])
    view._ctx.set_active([outside.uuid, local.uuid])
    await view.open_item("picked:test", "collection")
    detail = cast("dict[str, Any]", view.detail)
    assert detail["state"] == "installed" and detail["active"] is False

    await view.toggleCollection()
    assert set(view._ctx.active_uuids) == {
        steamcmd,
        dependency,
        outside.uuid,
        local.uuid,
    }
    assert cast("dict[str, Any]", view.detail)["active"] is True

    await view.toggleCollection()
    assert set(view._ctx.active_uuids) == {outside.uuid, local.uuid}

    await view.activateOnlyCollection()
    assert set(view._ctx.active_uuids) == {steamcmd, dependency, local.uuid}


async def test_collection_deactivation_removes_every_active_copy(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
    installed_mods: dict[str, ListedMod],
) -> None:
    view, catalog, _ = panel
    catalog.collection_complete = True
    view._ctx.load(installed_mods, list(installed_mods))
    await view.open_item("picked:test", "collection")
    assert cast("dict[str, Any]", view.detail)["active"] is True

    await view.toggleCollection()

    assert view._ctx.active_uuids == []
    assert cast("dict[str, Any]", view.detail)["active"] is False


async def test_activate_only_collection_keeps_dependents_of_retained_duplicate(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
    installed_mods: dict[str, ListedMod],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    from PySide6.QtWidgets import QMessageBox

    view, catalog, _ = panel
    catalog.collection_complete = True
    steamcmd, steam, dependent = installed_mods
    mod = cast("AboutXmlMod", installed_mods[dependent])
    mod.about_rules = BaseRules(
        dependencies={
            CaseInsensitiveStr("test.mod1"): DependencyMod(
                name="Dependency", package_id=CaseInsensitiveStr("test.mod1")
            )
        }
    )
    view._ctx.load(installed_mods, [steamcmd, steam, dependent])
    view._ctx.diagnostics_service.rebuild()
    prompts: list[object] = []

    async def accept_dependents(*args: object) -> tuple[int, None]:
        prompts.append(args)
        return QMessageBox.StandardButton.Yes, None

    monkeypatch.setattr(
        "pxmodrim.ui.components.mod_activation.await_dialog", accept_dependents
    )
    await view.open_item("picked:test", "collection")
    await view.activateOnlyCollection()

    assert not prompts
    assert set(view._ctx.active_uuids) == {steamcmd, dependent}
    assert cast("dict[str, Any]", view.detail)["active"] is True


async def test_partly_downloaded_collection_has_no_activation(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
    installed_mods: dict[str, ListedMod],
) -> None:
    view, catalog, _ = panel
    catalog.collection_complete = True
    steamcmd, _, _ = installed_mods
    view._ctx.load({steamcmd: installed_mods[steamcmd]}, [])
    await view.open_item("picked:test", "collection")
    assert cast("dict[str, Any]", view.detail)["state"] == "missing"
    await view.toggleCollection()
    await view.activateOnlyCollection()
    assert view._ctx.active_uuids == []


async def test_incomplete_collection_cannot_activate_only_its_available_members(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
    installed_mods: dict[str, ListedMod],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    view, catalog, _ = panel
    view._ctx.load(installed_mods, list(installed_mods))
    active_before = view._ctx.active_uuids

    async def incomplete_plan(
        mod_ids: list[str], collection_ids: list[str]
    ) -> DownloadPlan:
        return DownloadPlan([], ["1"], ["2"], [], {}, False)

    monkeypatch.setattr(catalog, "plan", incomplete_plan)
    await view.open_item("picked:test", "collection")
    assert cast("dict[str, Any]", view.detail)["state"] == "missing"
    await view.toggleCollection()
    await view.activateOnlyCollection()
    assert view._ctx.active_uuids == active_before


async def test_installed_changes_during_fetch_are_loaded_after_it_finishes(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
    installed_mods: dict[str, ListedMod],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    view, catalog, _ = panel
    await view.load()
    catalog._ctx = view._ctx
    steamcmd, _, _ = installed_mods
    view._ctx.load({steamcmd: installed_mods[steamcmd]}, [])
    started, release = asyncio.Event(), asyncio.Event()
    requested: list[list[str]] = []

    async def installed() -> list[CatalogMod]:
        ids = list(catalog._installed())
        requested.append(ids)
        if len(requested) == 1:
            started.set()
            await release.wait()
        items = {"1": catalog.mod_item, "2": catalog.dependency}
        return [items[pid] for pid in ids]

    monkeypatch.setattr(catalog, "installed_with_updates", installed)
    view._tab = "Installed"
    task = asyncio.create_task(view.load())
    async with asyncio.timeout(3):
        await started.wait()
        view._ctx.load(installed_mods, [])
        catalog._installed_index = None
        await view._on_installed_changed(None)
        release.set()
        await task
    assert requested == [["1"], ["1", "2"]]
    assert {row["itemId"] for row in view.mods_model._rows} == {"1", "2"}


async def test_installed_changes_preserve_details_and_reload_when_going_back(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
    installed_mods: dict[str, ListedMod],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    view, catalog, _ = panel
    catalog._ctx = view._ctx
    steamcmd, _, _ = installed_mods
    view._ctx.load({steamcmd: installed_mods[steamcmd]}, [])

    async def installed() -> list[CatalogMod]:
        items = {"1": catalog.mod_item, "2": catalog.dependency}
        return [items[pid] for pid in catalog._installed()]

    monkeypatch.setattr(catalog, "installed_with_updates", installed)
    await view.select_tab("Installed")
    await view.open_item("1", "mod")
    view._ctx.load(installed_mods, [])
    catalog._installed_index = None
    await view._on_installed_changed(None)
    assert view.hasDetail and cast("dict[str, Any]", view.detail)["itemId"] == "1"
    await view.back()
    assert not view.hasDetail
    assert {row["itemId"] for row in view.mods_model._rows} == {"1", "2"}


@pytest.mark.parametrize("failure", [False, True])
async def test_download_plan_finishing_after_destruction_is_discarded(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
    monkeypatch: pytest.MonkeyPatch,
    failure: bool,
) -> None:
    view, catalog, _ = panel
    await view.load()
    started, release = asyncio.Event(), asyncio.Event()
    plan = catalog.plan

    async def delayed_plan(
        mod_ids: list[str], collection_ids: list[str]
    ) -> DownloadPlan:
        started.set()
        await release.wait()
        if failure:
            raise CatalogError("Catalog unavailable", 503)
        return await plan(mod_ids, collection_ids)

    monkeypatch.setattr(catalog, "plan", delayed_plan)
    task = asyncio.create_task(view._download(["1"], []))
    async with asyncio.timeout(3):
        await started.wait()
        view.teardown()
        view._qml.setSource(QUrl())
        delete(view)
        release.set()
        await task
    assert not catalog.downloaded


async def test_revisiting_installed_within_freshness_does_not_refetch(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    view, catalog, _ = panel
    await asyncio.sleep(0)
    fetches = 0
    mods = [catalog.mod_item]

    async def installed() -> list[CatalogMod]:
        nonlocal fetches
        fetches += 1
        return mods

    catalog.installed_with_updates = installed  # type: ignore[method-assign]
    for tab in ("Installed", "Mods", "Installed"):
        await view.select_tab(tab)
    assert fetches == 1 and view.mods_model.count == 1


@pytest.mark.parametrize(
    ("control", "tooltip_binding"),
    [("PxButton", "ToolTip.text"), ("PxBadge", "tooltip")],
)
def test_card_controls_show_tooltips_lazily_on_hover(
    qapp: object,
    qtbot: QtBot,
    tmp_path: Path,
    control: str,
    tooltip_binding: str,
) -> None:
    owner = QWidget()
    owner.resize(300, 160)
    engine = create_qml_engine(owner)
    theme = Theme(engine)
    engine.rootContext().setContextProperty("Theme", theme)
    controls = QUrl.fromLocalFile(
        str(Path(__file__).parents[2] / "src/pxmodrim/ui/components/controls")
    ).toString()
    source = tmp_path / "Tooltips.qml"
    source.write_text(
        f'import QtQuick\nimport QtQuick.Controls\nimport "{controls}"\n'
        "Rectangle {\n"
        '    property string tipText: ""\n'
        f"    {control} {{\n"
        '        objectName: "target"; x: 20; y: 20; text: "Test action"\n'
        f"        {tooltip_binding}: parent.tipText\n"
        "    }\n"
        "}\n"
    )
    widget = QQuickWidget(engine, owner)
    widget.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
    widget.resize(300, 160)
    widget.setSource(QUrl.fromLocalFile(str(source)))
    owner.show()
    widget.show()
    root = widget.rootObject()
    assert root is not None
    target = root.findChild(QObject, "target")
    assert target is not None

    def tooltip() -> QObject | None:
        return next(
            (
                obj
                for obj in root.findChildren(QObject)
                if obj.inherits("QQuickToolTip")
            ),
            None,
        )

    try:
        qtbot.mouseMove(widget, QPoint(280, 140))
        assert tooltip() is None
        root.setProperty("tipText", "Help for this action")
        assert tooltip() is None
        qtbot.mouseMove(widget, QPoint(30, 25))
        qtbot.waitUntil(
            lambda: (tip := tooltip()) is not None and bool(tip.property("opened")),
            timeout=3000,
        )
        tip = tooltip()
        assert tip is not None
        assert tip.property("text") == "Help for this action"
        assert tip.property("delay") == theme.tooltipDelay
        assert tip.property("parent") == target
        accessible = QAccessible.queryAccessibleInterface(target)
        assert accessible is not None
        assert accessible.text(QAccessible.Text.Name) == "Test action"
        qtbot.mouseMove(widget, QPoint(280, 140))
        qtbot.waitUntil(lambda: tooltip() is None)
        qtbot.mouseMove(widget, QPoint(30, 25))
        qtbot.waitUntil(
            lambda: (tip := tooltip()) is not None and bool(tip.property("opened")),
            timeout=3000,
        )
        root.setProperty("tipText", "")
        qtbot.waitUntil(lambda: tooltip() is None)
    finally:
        widget.setSource(QUrl())
        owner.close()
        owner.deleteLater()
