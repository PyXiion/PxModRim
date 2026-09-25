from __future__ import annotations

from pxmodrim.core.models.view.sidebar import AllModsEntry, ProviderModsEntry


def test_sidebar_entries_own_their_visible_uuids() -> None:
    first = AllModsEntry()
    second = AllModsEntry()

    first.visible_uuids.add("first")

    assert second.visible_uuids == set()
    provider = ProviderModsEntry("local", "Local", {"provider"})
    assert provider.visible_uuids == {"provider"}
    assert provider.count == 1
