from __future__ import annotations

import msgspec
import pytest
from PySide6.QtCore import QModelIndex, QObject, Qt

from pxmodrim.core.workshop import Author, CatalogCollection, CatalogMod, Preview
from pxmodrim.ui.plugins.workshop.models import (
    CatalogListModel,
    description_rich_text,
    item_row,
    safe_url,
)


@pytest.mark.parametrize(
    ("source", "expected"),
    [
        (
            "[b]Bold[/b] [i]italic[/i] [u]underlined[/u]",
            "<b>Bold</b> <i>italic</i> <u>underlined</u>",
        ),
        (
            "[h1]Heading[/h1][h2]Two[/h2][h3]Three[/h3]",
            "<h1>Heading</h1><h2>Two</h2><h3>Three</h3>",
        ),
        ("[list][*]one[*]two[/list]", "<ul><li>one</li><li>two</li></ul>"),
        ("<script>alert(1)</script>&", "&lt;script&gt;alert(1)&lt;/script&gt;&amp;"),
        (
            '[b]<img src="file:///secret">[/b]',
            "<b>&lt;img src=&quot;file:///secret&quot;&gt;</b>",
        ),
        ("[url=javascript:alert(1)]click[/url]", "click"),
        ("[url=file:///etc/passwd]secret[/url]", "secret"),
        ("[url=data:text/html,test]data[/url]", "data"),
        (
            '[url=https://example.com/?x="&y=1]safe[/url]',
            '<a href="https://example.com/?x=&quot;&amp;y=1">safe</a>',
        ),
        (
            "[url]https://example.com[/url]",
            '<a href="https://example.com">https://example.com</a>',
        ),
        (
            "[img]https://example.com/picture.png[/img]",
            '<a href="https://example.com/picture.png">Image</a>',
        ),
        ("[img]file:///secret[/img]", "Image"),
        ("[b]unfinished", "<b>unfinished</b>"),
        ("[unknown]<b>raw</b>[/unknown]", "[unknown]&lt;b&gt;raw&lt;/b&gt;[/unknown]"),
    ],
)
def test_bbcode_emits_only_safe_markup(source: str, expected: str) -> None:
    assert description_rich_text(source) == expected


def test_text_descriptions_never_parse_bbcode_or_html() -> None:
    assert (
        description_rich_text("[b]<b>Text</b>[/b]\nnext", "text")
        == "[b]&lt;b&gt;Text&lt;/b&gt;[/b]<br>next"
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
