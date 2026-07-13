"""Sidebar entry models — shared between services and UI."""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import field

from pxmodrim.models.metadata.structures import ListedMod

PROVIDER_LABELS: dict[str, str] = {
    "local": "Local",
    "steam_cmd": "Steam Workshop",
    "core": "System / Core",
}


class SidebarEntry(ABC):
    """A filter entry in the sidebar. Knows which mods it represents."""

    visible_uuids: set[str] = field(default_factory=set)
    count: int = 0

    @property
    @abstractmethod
    def label(self) -> str: ...

    def refresh_count(self) -> None:
        self.count = len(self.visible_uuids)

    def update_uuids(
        self, all_mods: dict[str, ListedMod], active_uuids: set[str]
    ) -> None:
        """Override for entries that need re-computation on toggles (Active/Inactive)."""
        pass


class AllModsEntry(SidebarEntry):
    @property
    def label(self) -> str:
        return "All"


class ActiveModsEntry(SidebarEntry):
    @property
    def label(self) -> str:
        return "Active"

    def update_uuids(
        self, all_mods: dict[str, ListedMod], active_uuids: set[str]
    ) -> None:
        self.visible_uuids = set(active_uuids)
        self.refresh_count()


class InactiveModsEntry(SidebarEntry):
    @property
    def label(self) -> str:
        return "Inactive"

    def update_uuids(
        self, all_mods: dict[str, ListedMod], active_uuids: set[str]
    ) -> None:
        self.visible_uuids = set(all_mods) - set(active_uuids)
        self.refresh_count()


class ErrorModsEntry(SidebarEntry):
    @property
    def label(self) -> str:
        return "With errors"


class WarningModsEntry(SidebarEntry):
    @property
    def label(self) -> str:
        return "With warnings"


class ProviderModsEntry(SidebarEntry):
    def __init__(self, provider_id: str, label: str, uuids: set[str]) -> None:
        super().__init__()
        self._pid = provider_id
        self._label = label
        self.visible_uuids = uuids
        self.refresh_count()

    @property
    def label(self) -> str:
        return self._label
