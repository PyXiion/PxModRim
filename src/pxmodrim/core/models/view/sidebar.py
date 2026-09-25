"""Sidebar entry models — shared between services and UI."""

from __future__ import annotations

from abc import ABC, abstractmethod

PROVIDER_LABELS: dict[str, str] = {
    "local": "Local",
    "steam": "Steam Workshop",
    "steam_cmd": "Steam Workshop",
    "core": "System / Core",
}


class SidebarEntry(ABC):
    """A filter entry in the sidebar. Knows which mods it represents."""

    visible_uuids: set[str]
    count: int

    def __init__(self) -> None:
        self.visible_uuids = set()
        self.count = 0

    @property
    @abstractmethod
    def label(self) -> str: ...

    def refresh_count(self) -> None:
        self.count = len(self.visible_uuids)


class AllModsEntry(SidebarEntry):
    @property
    def label(self) -> str:
        return "All"


class ActiveModsEntry(SidebarEntry):
    @property
    def label(self) -> str:
        return "Active"


class InactiveModsEntry(SidebarEntry):
    @property
    def label(self) -> str:
        return "Inactive"


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
