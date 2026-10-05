from __future__ import annotations

from pathlib import Path
from typing import Any, cast

import msgspec
import pytest

from pxmodrim.core.config import AppConfig, ConfigService
from pxmodrim.core.workshop import (
    CatalogCollection,
    CatalogMod,
    CatalogPage,
    CatalogQuery,
)


def test_mod_null_fields_are_not_invented(mod_payload: dict[str, Any]) -> None:
    for key in [
        "preview_url",
        "created_at",
        "updated_at",
        "file_size",
        "subscriptions",
        "votes",
        "package_id",
    ]:
        mod_payload[key] = None
    mod_payload["author"] = {
        "id": "76561198000000000",
        "name": None,
        "profile_url": None,
    }
    mod = msgspec.json.decode(msgspec.json.encode(mod_payload), type=CatalogMod)
    assert mod.author.name is None
    assert mod.votes is None
    assert mod.updated_at is None
    assert mod.file_size is None
    assert msgspec.json.decode(msgspec.json.encode(mod)) == mod_payload
    with pytest.raises(AttributeError):
        cast(Any, mod).title = "Changed"


def test_steam_collection_and_empty_page(collection_payload: dict[str, Any]) -> None:
    collection_payload.update(
        {
            "id": "steam:123",
            "source": "steam",
            "steam_id": "123",
            "description_format": "bbcode",
            "workshop_url": "https://steamcommunity.com/sharedfiles/filedetails/?id=123",
            "member_ids": ["2009463077", "steam:456"],
            "member_count": 2,
        }
    )
    collection = msgspec.json.decode(
        msgspec.json.encode(collection_payload), type=CatalogCollection
    )
    assert collection.kind == "collection"
    assert collection.member_ids == ["2009463077", "steam:456"]
    assert msgspec.json.decode(msgspec.json.encode(collection)) == collection_payload
    page = msgspec.json.decode(
        b'{"items":[],"total":0,"next_cursor":null}', type=CatalogPage[CatalogMod]
    )
    assert page.items == [] and page.total == 0 and page.next_cursor is None


def test_config_default_loads_existing_files_and_persists_override(
    tmp_path: Path,
) -> None:
    service = ConfigService(tmp_path)
    (tmp_path / "config.json").write_bytes(
        b'{"schema_version":1,"workshop_parallel_items":3}'
    )
    config = service.load("config.json", AppConfig)
    assert config.workshop_catalog_url == "https://api.modrim.pyxiion.dev"
    assert config.workshop_parallel_items == 3
    config.workshop_catalog_url = ""
    service.save("config.json", config)
    assert service.load("config.json", AppConfig).workshop_catalog_url == ""


def test_query_defaults_and_frozen_contract() -> None:
    query = CatalogQuery()
    assert (
        query.query == ""
        and query.sort == "popular"
        and query.limit == 24
        and query.source == "all"
    )
    assert query.tag is None and query.version is None and query.cursor is None
    with pytest.raises(AttributeError):
        cast(Any, query).limit = 50
