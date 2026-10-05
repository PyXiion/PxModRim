from __future__ import annotations

import msgspec

from pxmodrim.core.workshop import Author, CatalogMod
from pxmodrim.ui.plugins.workshop.installed import PAGE, InstalledList


def _mod(
    id: str, title: str, author: str = "Someone", tags: list[str] | None = None
) -> CatalogMod:
    return CatalogMod(
        id=id,
        source="steam",
        title=title,
        author=Author(None, author, None),
        description="",
        description_format="bbcode",
        preview_url=None,
        previews=[],
        workshop_url="",
        tags=tags or [],
        supported_versions=[],
        created_at=None,
        updated_at=None,
        file_size=None,
        subscriptions=None,
        votes=None,
        dependencies=[],
        package_id=None,
        incompatible=False,
    )


def test_search_matches_title_author_id_and_tags_case_insensitively() -> None:
    installed = InstalledList()
    installed.replace(
        [
            _mod("11", "Harmony", "Brrainz"),
            _mod("12", "HugsLib", tags=["Library"]),
            _mod("999", "Other"),
        ]
    )
    for query, expected in [
        ("harm", ["11"]),
        ("BRRAINZ", ["11"]),
        ("library", ["12"]),
        ("999", ["999"]),
        ("", ["11", "12", "999"]),
        ("nothing", []),
    ]:
        installed.search(query)
        assert [m.id for m in installed.visible()] == expected, query


def test_paging_reveals_one_page_at_a_time_and_search_restarts_it() -> None:
    installed = InstalledList()
    base = _mod("0", "Mod")
    installed.replace(
        [msgspec.structs.replace(base, id=str(i)) for i in range(PAGE * 2 + 5)]
    )
    assert len(installed.visible()) == PAGE and installed.has_more
    second = installed.next_page()
    assert len(second) == PAGE and second[0].id == str(PAGE)
    assert len(installed.next_page()) == 5 and not installed.has_more
    assert installed.next_page() == []
    installed.search("")
    assert len(installed.visible()) == PAGE and installed.total == PAGE * 2 + 5
