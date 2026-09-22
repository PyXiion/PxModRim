from __future__ import annotations

from pathlib import Path

import pytest

from pxmodrim.core.config import PathConfig
from pxmodrim.core.models.view.sidebar import PROVIDER_LABELS
from pxmodrim.core.providers import SteamWorkshopModProvider, create_providers


@pytest.mark.asyncio
async def test_workshop_path_has_distinct_provider_and_local_roots(
    tmp_path: Path,
) -> None:
    local_path = tmp_path / "Mods"
    workshop_path = tmp_path / "workshop"
    about_path = workshop_path / "123456" / "About"
    about_path.mkdir(parents=True)
    (about_path / "About.xml").write_text(
        """<?xml version="1.0" encoding="utf-8"?>
<ModMetaData>
  <name>Workshop Mod</name>
  <author>Author</author>
  <packageId>author.workshop</packageId>
  <supportedVersions><li>1.5</li></supportedVersions>
</ModMetaData>
""",
        encoding="utf-8",
    )
    (about_path / "PublishedFileId.txt").write_text("123456", encoding="utf-8")

    providers = create_providers(
        PathConfig(local=str(local_path), workshop=str(workshop_path))
    )
    providers_by_id = {provider.provider_id: provider for provider in providers}

    assert len(providers_by_id) == len(providers)
    assert providers_by_id["local"]._path == local_path
    assert providers_by_id["steam_cmd"]._path == local_path

    workshop_provider = providers_by_id["steam"]
    assert isinstance(workshop_provider, SteamWorkshopModProvider)
    assert workshop_provider._path == workshop_path
    assert PROVIDER_LABELS[workshop_provider.provider_id] == "Steam Workshop"

    discovered = await workshop_provider.discover("1.5")
    assert len(discovered) == 1
    mod = next(iter(discovered.values()))
    assert mod.mod_path == workshop_path / "123456"
    assert mod.provider_id == "steam"
