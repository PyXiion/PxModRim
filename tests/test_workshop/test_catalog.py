from __future__ import annotations

import asyncio
from pathlib import Path
from typing import Any

import httpx
import msgspec
import pytest

from pxmodrim.core.config import AppConfig, ConfigService
from pxmodrim.core.context import CoreContext
from pxmodrim.core.downloads.manager import download_manager
from pxmodrim.core.downloads.steam import SteamDownloader, WorkshopSyncState
from pxmodrim.core.downloads.types import DownloadResult
from pxmodrim.core.mod_service import ModService
from pxmodrim.core.models.metadata.structures import ListedMod
from pxmodrim.core.workshop import (
    CatalogClient,
    CatalogError,
    CatalogMod,
    CatalogSettings,
    DownloadPlan,
    WorkshopCatalog,
)


def make_context(tmp_path: Path) -> tuple[CoreContext, SteamDownloader]:
    config = AppConfig()
    config.paths.local = str(tmp_path / "Mods")
    config.paths.workshop = str(tmp_path / "Workshop")
    ctx = CoreContext(config, ConfigService(tmp_path))
    ctx._mod_service = ModService(ctx, [])
    downloader = SteamDownloader()
    downloader.setup(ctx)
    downloader._sync = WorkshopSyncState(synced={"1": 200, "2": 50})
    mods = [
        ListedMod(_mod_path=tmp_path / "Mods" / "1"),
        ListedMod(_mod_path=tmp_path / "Mods" / "2"),
        ListedMod(_mod_path=tmp_path / "Workshop" / "3", provider_id="workshop"),
        ListedMod(_mod_path=tmp_path / "Mods" / "4"),
        ListedMod(_mod_path=tmp_path / "Mods" / "NotWorkshop"),
    ]
    ctx.load({mod.uuid: mod for mod in mods}, [mods[0].uuid])
    return ctx, downloader


def decode_mod(
    payload: dict[str, Any], id: str, updated: int | None = 100
) -> CatalogMod:
    return msgspec.json.decode(
        msgspec.json.encode({**payload, "id": id, "updated_at": updated}),
        type=CatalogMod,
    )


async def test_install_state_uses_sync_not_directory_mtime(
    tmp_path: Path, mod_payload: dict[str, Any]
) -> None:
    ctx, _ = make_context(tmp_path)
    catalog = WorkshopCatalog()
    catalog.setup(ctx)
    assert catalog.install_state(decode_mod(mod_payload, "1")) == "installed"
    assert catalog.install_state(decode_mod(mod_payload, "2")) == "outdated"
    assert catalog.install_state(decode_mod(mod_payload, "3")) == "installed"
    assert catalog.install_state(decode_mod(mod_payload, "4")) == "installed"
    assert catalog.install_state(decode_mod(mod_payload, "5")) == "missing"
    assert catalog.install_state(decode_mod(mod_payload, "2", None)) == "installed"
    assert catalog.install_state(decode_mod(mod_payload, "2", 50)) == "installed"
    await catalog.shutdown()


async def test_plan_keeps_dependency_order_and_incomplete_flags(
    tmp_path: Path, mod_payload: dict[str, Any], collection_payload: dict[str, Any]
) -> None:
    ctx, _ = make_context(tmp_path)
    active_before = ctx.active_uuids
    requests: list[httpx.Request] = []

    def handler(request: httpx.Request) -> httpx.Response:
        requests.append(request)
        return httpx.Response(
            200,
            json={
                "roots": ["5", "picked:starter"],
                "items": {
                    **{
                        id: {
                            **mod_payload,
                            "id": id,
                            "updated_at": 100,
                            "title": f"Title {id}",
                        }
                        for id in ["1", "2", "3", "4", "5"]
                    },
                    "picked:starter": collection_payload,
                },
                "mod_ids": ["1", "2", "3", "4", "5"],
                "unavailable_ids": ["99"],
                "incomplete_collection_ids": ["picked:starter"],
                "is_complete": False,
            },
        )

    catalog = WorkshopCatalog(
        lambda url: CatalogClient(url, httpx.MockTransport(handler))
    )
    catalog.setup(ctx)
    try:
        plan = await catalog.plan(["5"], ["picked:starter"])
        assert plan.to_download == ["2", "5"]
        assert plan.already_current == ["1", "3", "4"]
        assert plan.unavailable_ids == ["99"]
        assert plan.incomplete_collection_ids == ["picked:starter"]
        assert not plan.is_complete
        assert plan.titles["2"] == "Title 2"
        assert ctx.active_uuids == active_before
        assert msgspec.json.decode(requests[0].content) == {
            "ids": ["5"],
            "collection_ids": ["picked:starter"],
        }
    finally:
        await catalog.shutdown()


async def test_installed_metadata_uses_batch(
    tmp_path: Path, mod_payload: dict[str, Any]
) -> None:
    ctx, _ = make_context(tmp_path)
    requested: list[str] = []

    def handler(request: httpx.Request) -> httpx.Response:
        requested.extend(
            msgspec.json.decode(request.content, type=dict[str, list[str]])["ids"]
        )
        return httpx.Response(
            200,
            json={
                "items": [{**mod_payload, "id": "2", "updated_at": 100}],
                "unavailable_ids": ["1", "3", "4"],
            },
        )

    catalog = WorkshopCatalog(
        lambda url: CatalogClient(url, httpx.MockTransport(handler))
    )
    catalog.setup(ctx)
    try:
        installed = await catalog.installed_with_updates()
        assert requested == ["1", "2", "3", "4"]
        assert catalog.install_state(installed[0]) == "outdated"
    finally:
        await catalog.shutdown()


async def test_download_delegates_without_activation(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    ctx, _ = make_context(tmp_path)
    catalog = WorkshopCatalog()
    catalog.setup(ctx)
    calls: list[list[str]] = []
    result = DownloadResult(succeeded=["2"], failed=[])

    async def download(ids: list[str]) -> DownloadResult:
        calls.append(ids)
        return result

    monkeypatch.setattr(download_manager(ctx), "download_mods", download)
    active_before = ctx.active_uuids
    try:
        plan = DownloadPlan(["2"], ["1"], [], [], {"2": "Mod"}, True)
        assert await catalog.download(plan) is result
        assert calls == [["2"]]
        assert ctx.active_uuids == active_before
    finally:
        await catalog.shutdown()


async def test_empty_url_disabled_without_constructing_client(tmp_path: Path) -> None:
    ctx, _ = make_context(tmp_path)
    clients: list[str] = []

    def factory(url: str) -> CatalogClient:
        clients.append(url)
        raise AssertionError("Disabled catalog must not create a client")

    catalog = WorkshopCatalog(factory)
    catalog.setup(ctx)
    catalog.set_base_url("")
    try:
        assert not catalog.configured
        for operation in [
            catalog.discover(),
            catalog.installed_with_updates(),
            catalog.plan(["1"], []),
        ]:
            with pytest.raises(CatalogError, match="disabled"):
                await operation
        assert clients == []
    finally:
        await catalog.shutdown()


async def test_configuration_and_installed_events(
    tmp_path: Path, mod_payload: dict[str, Any]
) -> None:
    ctx, _ = make_context(tmp_path)
    transports: list[httpx.AsyncClient] = []
    urls: list[str] = []

    def factory(url: str) -> CatalogClient:
        urls.append(url)
        client = CatalogClient(
            url, httpx.MockTransport(lambda _: httpx.Response(200, json=mod_payload))
        )
        transports.append(client._http)
        return client

    catalog = WorkshopCatalog(factory)
    catalog.setup(ctx)
    changed: list[str] = []
    installed: list[None] = []
    catalog.catalog_url_changed.connect(changed.append)
    catalog.installed_changed.connect(installed.append)
    await catalog.mod("2009463077")
    catalog.set_base_url(" https://other.example/ ")
    assert catalog.settings.value.url == "https://other.example"
    stored = ConfigService(tmp_path).load_existing(
        "plugins/workshop_catalog.json", CatalogSettings
    )
    assert stored == CatalogSettings("https://other.example")
    await catalog.mod("2009463077")
    catalog.set_base_url("")
    assert not catalog.configured
    with pytest.raises(CatalogError, match="disabled"):
        await catalog.discover()
    ctx.mod_service.mods_changed.emit(None)
    download_manager(ctx).download_finished.emit(DownloadResult([], []))
    assert installed == [None, None]
    assert changed == ["https://other.example", ""]
    assert urls == ["https://api.modrim.pyxiion.dev", "https://other.example"]
    await catalog.shutdown()
    assert all(transport.is_closed for transport in transports)
    ctx.mod_service.mods_changed.emit(None)
    assert installed == [None, None]


async def test_cached_serves_remembered_value_before_refreshing(
    tmp_path: Path,
) -> None:
    ctx, _ = make_context(tmp_path)
    catalog = WorkshopCatalog()
    catalog.setup(ctx)
    values = iter(["first", "second", "second"])
    calls = 0

    async def produce() -> str:
        nonlocal calls
        calls += 1
        return next(values)

    async def seen(fresh_for: float) -> list[str]:
        return [value async for value in catalog.cached("k", produce, fresh_for)]

    try:
        assert await seen(300) == ["first"]
        assert await seen(300) == ["first"] and calls == 1
        assert await seen(0) == ["first", "second"] and calls == 2
        assert await seen(0) == ["second"] and calls == 3
    finally:
        await catalog.shutdown()


async def test_enqueue_runs_batches_in_order_after_the_running_download(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    ctx, downloader = make_context(tmp_path)
    catalog = WorkshopCatalog()
    catalog.setup(ctx)
    manager = download_manager(ctx)
    manager.register(downloader)
    calls: list[list[str]] = []
    busy = True

    async def download(ids: list[str]) -> DownloadResult:
        calls.append(ids)
        return DownloadResult(succeeded=ids, failed=[])

    monkeypatch.setattr(manager, "download_mods", download)
    monkeypatch.setattr(type(manager), "is_downloading", property(lambda _self: busy))
    try:
        catalog.enqueue(DownloadPlan(["5", "6"], [], [], [], {}, True))
        catalog.enqueue(DownloadPlan(["6", "7"], [], [], [], {}, True))
        assert catalog.queued_ids == {"5", "6", "7"}
        await asyncio.sleep(0.05)
        assert calls == []
        busy = False
        manager.busy_changed.emit(False)
        await asyncio.wait_for(catalog._runner, 2)  # type: ignore[arg-type]
        assert calls == [["5", "6"], ["7"]] and not catalog.queued_ids
    finally:
        await catalog.shutdown()


async def test_cached_values_survive_a_restart_and_corrupt_files_are_ignored(
    tmp_path: Path,
) -> None:
    ctx, _ = make_context(tmp_path)

    async def produce() -> list[int]:
        return [1, 2]

    async def must_not_run() -> list[int]:
        raise AssertionError("A fresh value on disk must not hit the catalog")

    first = WorkshopCatalog()
    first.setup(ctx)
    assert [v async for v in first.cached("k", produce, value_type=list[int])] == [
        [1, 2]
    ]

    second = WorkshopCatalog()
    second.setup(ctx)
    assert [
        v async for v in second.cached("k", must_not_run, value_type=list[int])
    ] == [[1, 2]]

    for file in (tmp_path / "workshop-cache").glob("*.json"):
        file.write_text("{not json")
    third = WorkshopCatalog()
    third.setup(ctx)
    assert [v async for v in third.cached("k", produce, value_type=list[int])] == [
        [1, 2]
    ]


async def test_equal_cache_refresh_keeps_the_displayed_mods_and_refreshes_age(
    tmp_path: Path, mod_payload: dict[str, Any]
) -> None:
    ctx, _ = make_context(tmp_path)
    catalog = WorkshopCatalog()
    catalog.setup(ctx)
    calls = 0

    async def produce() -> list[CatalogMod]:
        nonlocal calls
        calls += 1
        return [decode_mod(mod_payload, "1")]

    try:
        initial = await anext(catalog.cached("installed", produce))
        refreshed = [
            value async for value in catalog.cached("installed", produce, fresh_for=0)
        ]
        assert len(refreshed) == 1 and refreshed[0] is initial
        current = await anext(catalog.cached("installed", produce))
        assert current is initial and calls == 2
    finally:
        await catalog.shutdown()


async def test_disk_cache_is_reused_in_memory_after_first_paint(
    tmp_path: Path, mod_payload: dict[str, Any]
) -> None:
    ctx, _ = make_context(tmp_path)

    async def produce() -> list[CatalogMod]:
        return [decode_mod(mod_payload, "1")]

    async def must_not_run() -> list[CatalogMod]:
        raise AssertionError("A fresh cached response must not hit the catalog")

    first = WorkshopCatalog()
    first.setup(ctx)
    second = WorkshopCatalog()
    second.setup(ctx)
    try:
        await anext(first.cached("installed", produce, value_type=list[CatalogMod]))
        displayed = await anext(
            second.cached("installed", must_not_run, value_type=list[CatalogMod])
        )
        for file in (tmp_path / "workshop-cache").glob("*.json"):
            file.write_text("{not json")
        current = await anext(
            second.cached("installed", must_not_run, value_type=list[CatalogMod])
        )
        assert current is displayed
    finally:
        await first.shutdown()
        await second.shutdown()


def test_catalog_url_moves_out_of_the_old_shared_config(tmp_path: Path) -> None:
    ctx, _ = make_context(tmp_path)
    (tmp_path / "config.json").write_bytes(
        b'{"schema_version":1,"workshop_catalog_url":"http://localhost:8787"}'
    )
    catalog = WorkshopCatalog()
    catalog.setup(ctx)
    assert catalog.settings.value.url == "http://localhost:8787"
    stored = ConfigService(tmp_path).load_existing(
        "plugins/workshop_catalog.json", CatalogSettings
    )
    assert stored == CatalogSettings("http://localhost:8787")
