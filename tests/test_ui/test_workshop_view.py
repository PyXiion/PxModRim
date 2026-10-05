from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator
from typing import Any, cast

import msgspec
import pytest
from PySide6.QtCore import QUrl
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import QWidget

from pxmodrim.core.config import AppConfig, ConfigService
from pxmodrim.core.context import CoreContext
from pxmodrim.core.downloads import DownloadManager
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
        super().__init__(lambda: ctx.config)
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
        return CollectionDetail(
            self.collection_item, [self.mod_item], ["missing"], False
        )

    async def installed_with_updates(self) -> list[CatalogMod]:
        return [self.mod_item]

    def install_state(self, mod: CatalogMod) -> InstallState:
        return "outdated" if mod.id == "1" else "missing"

    async def plan(self, mod_ids: list[str], collection_ids: list[str]) -> DownloadPlan:
        return DownloadPlan(
            ["2", "1"],
            [],
            ["missing"] if collection_ids else [],
            collection_ids,
            {"1": "Mod", "2": "Dependency"},
            not collection_ids,
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
    engine.warnings.connect(warnings.extend)
    view = WorkshopViewPanel(ctx, engine, owner, catalog=catalog)
    view.resize(1100, 800)
    yield view, catalog, warnings
    view._qml.setSource(QUrl())
    owner.deleteLater()


async def test_view_loads_qml_and_discovery_without_errors(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    view, _, warnings = panel
    await asyncio.sleep(0)
    assert view._qml.status() == QQuickWidget.Status.Ready
    assert view.mods_model.count == 1 and view.collections_model.count == 1
    assert not warnings, [warning.toString() for warning in warnings]


async def test_view_paging_filters_and_catalog_errors(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    view, catalog, _ = panel
    await asyncio.sleep(0)
    await view.selectTab("Mods")
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
    assert detail["description"] == "<b>Safe description</b>"
    assert detail["actionLabel"] == "Update"
    assert view.members_model.count == 1
    await view.downloadItem("1", "mod")
    assert catalog.downloaded == ["2", "1"]
    assert not warnings, [warning.toString() for warning in warnings]


async def test_incomplete_collection_requires_explicit_partial_download(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    view, catalog, warnings = panel
    await asyncio.sleep(0)
    await view.open_item("picked:test", "collection")
    detail = cast("dict[str, Any]", view.detail)
    assert not detail["complete"] and "missing" in detail["warning"]
    assert detail["description"] == "&lt;b&gt;Plain text&lt;/b&gt;"
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
    view._ctx.update_config(AppConfig(workshop_catalog_url=""))
    await view.load()
    assert not view.configured and not catalog.queries


async def test_settings_saves_and_applies_catalog_url(
    panel: tuple[WorkshopViewPanel, FakeCatalog, list[Any]],
) -> None:
    from pxmodrim.ui.panels.settings_panel import SettingsPanel

    view, catalog, _ = panel
    await asyncio.sleep(0)
    view._ctx.plugins.register(catalog)
    settings = SettingsPanel(view._ctx, view._qml.engine(), view)
    values = dict(cast("dict[str, Any]", settings._backend.initial))
    values["catalogUrl"] = " https://catalog.example.test/ "
    settings._save(values)
    assert settings.get_config().workshop_catalog_url == "https://catalog.example.test/"
    assert view._ctx.config.workshop_catalog_url == "https://catalog.example.test"
    await asyncio.sleep(0)
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
    await view.selectTab("Installed")
    assert view.mods_model.count == 3 and fetches == 1

    await view.filter("hugs", "", "all", "popular", "")
    assert view.mods_model.count == 1 and view.tab == "Installed" and fetches == 1
    await view.filter("", "", "all", "popular", "")
    assert view.mods_model.count == 3 and fetches == 1
