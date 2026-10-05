from __future__ import annotations

import msgspec
import pytest
from PySide6.QtCore import QModelIndex, QObject, Qt

from pxmodrim.core.workshop import Author, CatalogCollection, CatalogMod, Preview, Votes
from pxmodrim.ui.panels.mod_info_data import description_to_html
from pxmodrim.ui.plugins.workshop.models import (
    CatalogListModel,
    item_row,
    safe_url,
)


@pytest.mark.parametrize(
    "url",
    [
        "javascript:alert(1)",
        "file:///tmp/test",
        "data:text/plain,hi",
        "//example.com",
        "https:///path",
        "https://example.com/\nsecret",
        "https://[bad",
    ],
)
def test_link_scheme_filter_rejects_unsafe_urls(url: str) -> None:
    assert safe_url(url) == ""


@pytest.mark.parametrize(
    "url", ["https://example.com/path", "http://example.com", "HTTPS://example.com"]
)
def test_link_scheme_filter_accepts_web_urls(url: str) -> None:
    assert safe_url(url) == url


def _mod(pid: str) -> CatalogMod:
    return CatalogMod(
        id=pid,
        source="steam",
        title=f"Mod {pid}",
        author=Author("1", "Author", None),
        description="[b]Description[/b]",
        description_format="bbcode",
        preview_url=None,
        previews=[],
        workshop_url=f"https://steamcommunity.com/sharedfiles/filedetails/?id={pid}",
        tags=["Utility"],
        supported_versions=["1.5"],
        created_at=None,
        updated_at=100,
        file_size="1048576",
        subscriptions=None,
        votes=None,
        dependencies=[],
        package_id=None,
        incompatible=False,
    )


def test_roles_paging_append_and_reset(qapp: object) -> None:
    owner = QObject()
    model = CatalogListModel(owner)
    rows = [item_row(_mod(pid), lambda _: "outdated", "1.6") for pid in ["1", "2", "3"]]
    model.set_page(rows[:2], 10, "page2")
    assert model.hasMore and model.count == 2 and model.total == 10
    names = {bytes(name.data()).decode() for name in model.roleNames().values()}
    assert names == set(rows[0])
    role = next(
        role
        for role, name in model.roleNames().items()
        if bytes(name.data()) == b"itemId"
    )
    assert model.data(model.index(0), role) == "1"
    model.set_page(rows[1:], 10, None, append=True)
    assert model.count == 3 and not model.hasMore
    assert model.data(model.index(2), role) == "3"
    assert model.data(QModelIndex(), role) is None
    assert model.data(model.index(0), Qt.ItemDataRole.DisplayRole) is None
    assert model.rowCount(model.index(0)) == 0
    model.set_page([], 0, None)
    assert model.count == 0 and model.total == 0


def test_row_shows_update_and_game_incompatibility() -> None:
    row = item_row(_mod("1"), lambda _: "outdated", "1.6")
    assert row["actionLabel"] == "Update" and row["incompatible"]
    assert row["fileSize"] == "1.0 MB"


@pytest.mark.parametrize(
    ("votes", "approval", "up", "down"),
    [
        (None, "Unrated", "0", "0"),
        (Votes(0, 0, 100), "Unrated", "0", "0"),
        (Votes(0, 1, None), "0%", "0", "1"),
        (Votes(1, 0, None), "100%", "1", "0"),
        (Votes(1, 2, None), "33%", "1", "2"),
        (Votes(2, 1, None), "67%", "2", "1"),
        (Votes(1_000, 3_000, None), "25%", "1,000", "3,000"),
    ],
)
def test_vote_approval_and_counts(
    votes: Votes | None, approval: str, up: str, down: str
) -> None:
    mod = msgspec.structs.replace(_mod("1"), votes=votes)
    row = item_row(mod, lambda _: "installed", "1.5")
    assert (row["votes"], row["votesUp"], row["votesDown"]) == (approval, up, down)


@pytest.mark.parametrize("description_format", ["bbcode", "text"])
def test_descriptions_use_shared_mod_info_renderer(description_format: str) -> None:
    mod = msgspec.structs.replace(_mod("1"), description_format=description_format)
    row = item_row(mod, lambda _: "missing", "1.5")
    assert row["description"] == description_to_html(
        mod.description, description_format
    )


def test_versions_are_unique_numeric_sorted_and_not_repeated_in_tags() -> None:
    mod = _mod("1")
    mod = msgspec.structs.replace(
        mod,
        supported_versions=["1.5", "1.10", "1.2", "1.5", "1.2.1"],
        tags=["Mod", "1.5", "Utility", "1.10", "1.2", "1.2.1"],
    )
    row = item_row(mod, lambda _: "missing", "1.5")
    assert row["versions"] == "1.2, 1.2.1, 1.5, 1.10"
    assert row["tags"] == "Mod · Utility"


def test_collection_image_prefers_own_then_first_image_then_member_collage() -> None:
    bare = CatalogCollection(
        id="steam:1",
        source="steam",
        steam_id="1",
        title="C",
        author=Author("1", "A", None),
        description="",
        description_format="bbcode",
        preview_url=None,
        previews=[
            Preview("video", "https://youtube.test/v"),
            Preview("image", "https://images.test/own.png"),
        ],
        workshop_url=None,
        tags=[],
        supported_versions=[],
        created_at=None,
        updated_at=None,
        member_ids=["10"],
        member_count=1,
        member_previews=["https://images.test/m.png", "javascript:alert(1)"],
    )
    row = item_row(bare, lambda _: "missing", "1.6")
    assert row["previewUrl"] == "https://images.test/own.png"
    assert row["collage"] == ["https://images.test/m.png"]
    assert (row["votes"], row["votesUp"], row["votesDown"]) == ("Unrated", "0", "0")
    assert row["active"] is False
