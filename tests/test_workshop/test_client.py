from __future__ import annotations

from typing import Any

import httpx
import msgspec
import pytest

from pxmodrim.core.workshop import (
    CatalogClient,
    CatalogCollection,
    CatalogError,
    CatalogMod,
    CatalogQuery,
)


async def test_decodes_every_endpoint(
    mod_payload: dict[str, Any], collection_payload: dict[str, Any]
) -> None:
    mod_page = {"items": [mod_payload], "total": 1, "next_cursor": "next-page"}
    collection_page = {"items": [collection_payload], "total": 1, "next_cursor": None}
    detail = {
        "collection": collection_payload,
        "members": [mod_payload],
        "unavailable_ids": ["99"],
        "is_complete": False,
    }
    steam_collection = {
        **collection_payload,
        "id": "steam:123",
        "source": "steam",
        "steam_id": "123",
        "description_format": "bbcode",
        "workshop_url": "https://steamcommunity.com/sharedfiles/filedetails/?id=123",
        "member_ids": ["2009463077", "456", "99"],
        "member_count": 3,
    }
    nested_collection = {
        **steam_collection,
        "id": "steam:456",
        "steam_id": "456",
        "member_ids": ["2009463077"],
        "member_count": 1,
    }
    steam_detail = {
        **detail,
        "collection": steam_collection,
        "members": [mod_payload, nested_collection],
    }
    resolution = {
        "roots": ["picked:starter"],
        "items": {"2009463077": mod_payload, "picked:starter": collection_payload},
        "mod_ids": ["2009463077"],
        "unavailable_ids": ["99"],
        "incomplete_collection_ids": ["picked:starter"],
        "is_complete": False,
    }
    responses = {
        "/catalog/discover": {"mods": mod_page, "collections": collection_page},
        "/catalog/mods": mod_page,
        "/catalog/collections": collection_page,
        "/catalog/mods/2009463077": mod_payload,
        "/catalog/collections/picked/starter": detail,
        "/catalog/collections/steam/123": steam_detail,
        "/catalog/resolve": resolution,
    }
    requests: list[httpx.Request] = []

    def handler(request: httpx.Request) -> httpx.Response:
        requests.append(request)
        return httpx.Response(200, json=responses[request.url.path])

    client = CatalogClient("https://catalog.example/", httpx.MockTransport(handler))
    try:
        discover = await client.discover()
        assert discover.mods.items[0].file_size == "18446744073709551615"
        assert discover.mods.items[0].votes is not None
        assert discover.mods.items[0].votes.positive_percent == 90
        assert discover.mods.items[0].previews[1].type == "video"
        assert discover.collections.items[0].description_format == "text"
        q = CatalogQuery("quality", "Libraries", "1.6", "updated", 50, "next", "picked")
        page = await client.mods(q)
        assert page.next_cursor == "next-page"
        assert dict(requests[-1].url.params) == {
            "q": "quality",
            "tag": "Libraries",
            "version": "1.6",
            "sort": "updated",
            "limit": "50",
            "cursor": "next",
            "source": "picked",
        }
        assert (await client.collections(q)).items[0].id == "picked:starter"
        mod = await client.mod("2009463077")
        assert mod is not None and mod.description == mod_payload["description"]
        assert mod.kind == "mod"
        for id in ["picked:starter", "steam:123"]:
            collection = await client.collection(id)
            assert collection is not None
            assert isinstance(collection.members[0], CatalogMod)
            if id == "steam:123":
                assert isinstance(collection.members[1], CatalogCollection)
            assert collection.unavailable_ids == ["99"]
            assert not collection.is_complete
        resolved = await client.resolve([], ["picked:starter"])
        assert resolved.roots == ["picked:starter"]
        assert resolved.mod_ids == ["2009463077"]
        assert isinstance(resolved.items["picked:starter"], CatalogCollection)
        assert resolved.incomplete_collection_ids == ["picked:starter"]
        assert not resolved.is_complete
        assert msgspec.json.decode(requests[-1].content) == {
            "ids": [],
            "collection_ids": ["picked:starter"],
        }
    finally:
        await client.shutdown()


@pytest.mark.parametrize("status", [400, 401, 404, 502, 503])
@pytest.mark.parametrize("json_body", [True, False])
async def test_error_statuses_are_never_empty_results(
    status: int, json_body: bool
) -> None:
    def handler(_: httpx.Request) -> httpx.Response:
        if json_body:
            return httpx.Response(
                status, json={"error": {"message": "Catalog unavailable"}}
            )
        return httpx.Response(status, text="<html>Upstream failure</html>")

    client = CatalogClient("https://catalog.example", httpx.MockTransport(handler))
    try:
        with pytest.raises(CatalogError) as exc:
            await client.mod("123")
        assert exc.value.status == status
        assert exc.value.message == str(exc.value)
        if json_body:
            assert exc.value.message == "Catalog unavailable"
        else:
            assert f"HTTP {status}" in exc.value.message
            assert "<html>" not in exc.value.message
    finally:
        await client.shutdown()


@pytest.mark.parametrize(
    "invalid", ["numeric_id", "bad_kind", "missing_field", "not_json"]
)
async def test_strict_wire_validation(
    mod_payload: dict[str, Any], invalid: str
) -> None:
    if invalid == "numeric_id":
        mod_payload["id"] = 123
    elif invalid == "bad_kind":
        mod_payload["kind"] = "collection"
    elif invalid == "missing_field":
        del mod_payload["title"]

    def handler(_: httpx.Request) -> httpx.Response:
        return (
            httpx.Response(200, content=b"not-json")
            if invalid == "not_json"
            else httpx.Response(200, json=mod_payload)
        )

    client = CatalogClient("https://catalog.example", httpx.MockTransport(handler))
    try:
        with pytest.raises(CatalogError, match="invalid response"):
            await client.mod("123")
    finally:
        await client.shutdown()


async def test_chunks_large_batch(mod_payload: dict[str, Any]) -> None:
    batches: list[list[str]] = []

    def handler(request: httpx.Request) -> httpx.Response:
        ids = msgspec.json.decode(request.content, type=dict[str, list[str]])["ids"]
        batches.append(ids)
        return httpx.Response(
            200,
            json={
                "items": [{**mod_payload, "id": id} for id in ids if id != "105"],
                "unavailable_ids": ["105"] if "105" in ids else [],
            },
        )

    client = CatalogClient("https://catalog.example", httpx.MockTransport(handler))
    ids = [str(id) for id in range(1, 206)]
    try:
        items, unavailable = await client.mods_batch(ids)
        assert [len(batch) for batch in batches] == [100, 100, 5]
        assert [item.id for item in items] == [id for id in ids if id != "105"]
        assert unavailable == ["105"]
        assert await client.mods_batch([]) == ([], [])
    finally:
        await client.shutdown()


async def test_transport_failure_is_catalog_error() -> None:
    def handler(request: httpx.Request) -> httpx.Response:
        raise httpx.ConnectError("offline", request=request)

    client = CatalogClient("https://catalog.example", httpx.MockTransport(handler))
    try:
        with pytest.raises(CatalogError, match="Cannot reach") as exc:
            await client.discover()
        assert exc.value.status is None
    finally:
        await client.shutdown()


async def test_empty_url_does_not_use_transport() -> None:
    requests: list[httpx.Request] = []

    def handler(request: httpx.Request) -> httpx.Response:
        requests.append(request)
        return httpx.Response(200)

    client = CatalogClient("", httpx.MockTransport(handler))
    try:
        with pytest.raises(CatalogError, match="disabled"):
            await client.discover()
        assert requests == []
    finally:
        await client.shutdown()


async def test_resolve_chunks_update_all_and_deduplicates_dependencies(
    mod_payload: dict[str, Any],
) -> None:
    requests: list[dict[str, list[str]]] = []
    ids = [str(id) for id in range(1, 104)]
    collections = [f"picked:pack-{index}" for index in range(11)]

    def handler(request: httpx.Request) -> httpx.Response:
        payload = msgspec.json.decode(request.content, type=dict[str, list[str]])
        requests.append(payload)
        mod_ids = ["999", *payload["ids"]]
        return httpx.Response(
            200,
            json={
                "roots": [*payload["ids"], *payload["collection_ids"]],
                "items": {
                    id: {
                        **mod_payload,
                        "id": id,
                        "dependencies": [] if id == "999" else ["999"],
                    }
                    for id in mod_ids
                },
                "mod_ids": mod_ids,
                "unavailable_ids": ["404"] if len(requests) == 2 else [],
                "incomplete_collection_ids": (
                    ["picked:pack-10"] if len(requests) == 2 else []
                ),
                "is_complete": len(requests) != 2,
            },
        )

    client = CatalogClient("https://catalog.example", httpx.MockTransport(handler))
    try:
        resolved = await client.resolve(ids, collections)
        assert [len(request["ids"]) for request in requests] == [50, 50, 3]
        assert [len(request["collection_ids"]) for request in requests] == [10, 1, 0]
        assert set(resolved.roots) == set(ids + collections)
        assert resolved.mod_ids == ["999", *ids]
        assert resolved.unavailable_ids == ["404"]
        assert resolved.incomplete_collection_ids == ["picked:pack-10"]
        assert not resolved.is_complete
    finally:
        await client.shutdown()
