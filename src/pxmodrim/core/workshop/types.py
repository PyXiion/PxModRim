from __future__ import annotations

from typing import Generic, Literal, TypeVar

import msgspec


class Author(msgspec.Struct, frozen=True):
    id: str | None
    name: str | None
    profile_url: str | None


class Preview(msgspec.Struct, frozen=True):
    type: Literal["image", "video"]
    url: str


class Votes(msgspec.Struct, frozen=True):
    up: int
    down: int
    positive_percent: float | None


class CatalogMod(msgspec.Struct, frozen=True, tag="mod", tag_field="kind"):
    id: str
    source: Literal["steam"]
    title: str
    author: Author
    description: str
    description_format: Literal["bbcode"]
    preview_url: str | None
    previews: list[Preview]
    workshop_url: str
    tags: list[str]
    supported_versions: list[str]
    created_at: int | None
    updated_at: int | None
    file_size: str | None
    subscriptions: int | None
    votes: Votes | None
    dependencies: list[str]
    package_id: str | None
    incompatible: bool

    @property
    def kind(self) -> Literal["mod"]:
        return "mod"


class CatalogCollection(
    msgspec.Struct, frozen=True, tag="collection", tag_field="kind"
):
    id: str
    source: Literal["steam", "picked"]
    steam_id: str | None
    title: str
    author: Author
    description: str
    description_format: Literal["bbcode", "text"]
    preview_url: str | None
    previews: list[Preview]
    workshop_url: str | None
    tags: list[str]
    supported_versions: list[str]
    created_at: int | None
    updated_at: int | None
    member_ids: list[str]
    member_count: int
    member_previews: list[str] = msgspec.field(default_factory=list)
    total_size: str | None = None

    @property
    def kind(self) -> Literal["collection"]:
        return "collection"


T = TypeVar("T")


class CatalogPage(msgspec.Struct, Generic[T], frozen=True):  # noqa: UP046
    # msgspec resolves postponed annotations using module globals, not PEP 695 scope.
    items: list[T]
    total: int
    next_cursor: str | None


class CollectionDetail(msgspec.Struct, frozen=True):
    collection: CatalogCollection
    members: list[CatalogMod | CatalogCollection]
    unavailable_ids: list[str]
    is_complete: bool


class Discover(msgspec.Struct, frozen=True):
    mods: CatalogPage[CatalogMod]
    collections: CatalogPage[CatalogCollection]


class ResolvedDownload(msgspec.Struct, frozen=True):
    roots: list[str]
    items: dict[str, CatalogMod | CatalogCollection]
    mod_ids: list[str]
    unavailable_ids: list[str]
    incomplete_collection_ids: list[str]
    is_complete: bool


class CatalogQuery(msgspec.Struct, frozen=True):
    query: str = ""
    tag: str | None = None
    version: str | None = None
    sort: str = "popular"
    limit: int = 24
    cursor: str | None = None
    source: str = "all"


InstallState = Literal["missing", "installed", "outdated"]


class DownloadPlan(msgspec.Struct, frozen=True):
    to_download: list[str]
    already_current: list[str]
    unavailable_ids: list[str]
    incomplete_collection_ids: list[str]
    titles: dict[str, str]
    is_complete: bool
