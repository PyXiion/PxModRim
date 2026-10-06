from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pxmodrim.core.workshop import CatalogMod

PAGE = 48


class InstalledList:
    """Installed Workshop mods with a text filter and incremental paging."""

    def __init__(self) -> None:
        self._mods: list[CatalogMod] = []
        self._matches: list[CatalogMod] = []
        self._query = ""
        self._shown = PAGE

    @property
    def mods(self) -> list[CatalogMod]:
        return self._mods

    @property
    def loaded(self) -> bool:
        return bool(self._mods)

    @property
    def total(self) -> int:
        return len(self._matches)

    @property
    def has_more(self) -> bool:
        return self._shown < len(self._matches)

    def replace(self, mods: list[CatalogMod]) -> None:
        self._mods = mods
        self._rematch()

    def search(self, query: str) -> None:
        self._query = query
        self._shown = PAGE
        self._rematch()

    def reset_paging(self) -> None:
        self._shown = PAGE

    def visible(self) -> list[CatalogMod]:
        return self._matches[: self._shown]

    def next_page(self) -> list[CatalogMod]:
        start = self._shown
        self._shown = min(start + PAGE, len(self._matches))
        return self._matches[start : self._shown]

    def _rematch(self) -> None:
        needle = self._query.casefold()
        self._matches = [mod for mod in self._mods if needle in _haystack(mod)]


def _haystack(mod: CatalogMod) -> str:
    return " ".join((mod.title, mod.author.name or "", mod.id, *mod.tags)).casefold()
