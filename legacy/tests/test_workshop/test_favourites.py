from __future__ import annotations

import msgspec

from pxmodrim.core.config import ConfigService
from pxmodrim.core.workshop import Author, CatalogCollection
from pxmodrim.core.workshop.favourites import FavouriteCollections


def _collection(cid: str, title: str = "Pack") -> CatalogCollection:
    return CatalogCollection(
        id=cid,
        source="steam",
        steam_id=cid.partition(":")[2],
        title=title,
        author=Author(None, "Curator", None),
        description="",
        description_format="text",
        preview_url=None,
        previews=[],
        workshop_url=None,
        tags=[],
        supported_versions=["1.6"],
        created_at=None,
        updated_at=None,
        member_ids=["1"],
        member_count=1,
    )


def test_favourites_persist_newest_first_and_survive_restart(
    config_service: ConfigService,
) -> None:
    favourites = FavouriteCollections(config_service)
    first, second = _collection("steam:1"), _collection("steam:2")
    favourites.set(first, True)
    favourites.set(second, True)
    favourites.set(second, True)

    reloaded = FavouriteCollections(config_service)
    assert [c.id for c in reloaded.items] == ["steam:2", "steam:1"]
    assert "steam:1" in reloaded

    reloaded.set(first, False)
    assert [c.id for c in FavouriteCollections(config_service).items] == ["steam:2"]
    assert "steam:1" not in reloaded


def test_remember_refreshes_only_starred_snapshots(
    config_service: ConfigService,
) -> None:
    favourites = FavouriteCollections(config_service)
    favourites.set(_collection("steam:1", "Old title"), True)
    saves: list[object] = []
    favourites.changed.connect(saves.append)

    favourites.remember(_collection("steam:1", "Old title"))
    favourites.remember(_collection("steam:2", "Not starred"))
    assert saves == []

    favourites.remember(_collection("steam:1", "New title"))
    reloaded = FavouriteCollections(config_service)
    assert [c.title for c in reloaded.items] == ["New title"]
    assert (
        msgspec.json.decode(
            (
                config_service.config_dir / "plugins/workshop_favourites.json"
            ).read_bytes()
        )["collections"][0]["id"]
        == "steam:1"
    )
