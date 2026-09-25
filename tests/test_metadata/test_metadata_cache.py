from __future__ import annotations

import os
from pathlib import Path
from unittest.mock import patch

import aiosqlite
import pytest

from pxmodrim.core.config import AppConfig
from pxmodrim.core.context import CoreContext
from pxmodrim.core.mod_service import ModService
from pxmodrim.core.models.metadata.structures import AboutXmlMod
from pxmodrim.core.providers import base as base_module
from pxmodrim.core.providers.local import LocalModProvider, SteamCmdModProvider
from pxmodrim.core.services.metadata_cache import (
    MetadataCache,
    normalize_path,
)

ABOUT_XML_V1 = """<?xml version="1.0" encoding="utf-8"?>
<ModMetaData>
  <name>Sample Mod One</name>
  <author>TestAuthor</author>
  <packageId>Author.SampleModOne</packageId>
  <supportedVersions>
    <li>1.4</li>
    <li>1.5</li>
  </supportedVersions>
  <description>Initial description</description>
  <modVersion>1.0.0</modVersion>
  <url>https://example.com/mod</url>
  <modIconPath>UI/Icon.png</modIconPath>
  <modDependencies>
    <li>
      <packageId>ludeon.rimworld</packageId>
      <displayName>RimWorld</displayName>
    </li>
  </modDependencies>
</ModMetaData>
"""

ABOUT_XML_V2 = """<?xml version="1.0" encoding="utf-8"?>
<ModMetaData>
  <name>Sample Mod One Updated</name>
  <author>TestAuthor</author>
  <packageId>Author.SampleModOne</packageId>
  <supportedVersions>
    <li>1.4</li>
    <li>1.5</li>
  </supportedVersions>
  <description>Updated description</description>
</ModMetaData>
"""

ABOUT_XML_TWO = """<?xml version="1.0" encoding="utf-8"?>
<ModMetaData>
  <name>Sample Mod Two</name>
  <author>OtherAuthor</author>
  <packageId>Author.SampleModTwo</packageId>
  <supportedVersions>
    <li>1.5</li>
  </supportedVersions>
  <description>Second mod description</description>
</ModMetaData>
"""


def _create_mod(mod_dir: Path, xml_content: str, pfid: str | None = None) -> Path:
    about_dir = mod_dir / "About"
    about_dir.mkdir(parents=True, exist_ok=True)
    xml_path = about_dir / "About.xml"
    xml_path.write_text(xml_content, encoding="utf-8")
    if pfid is not None:
        (about_dir / "PublishedFileId.txt").write_text(pfid, encoding="utf-8")
    return mod_dir


@pytest.mark.asyncio
async def test_warm_cache_avoids_reparse(tmp_path: Path) -> None:
    mods_root = tmp_path / "mods"
    mod_a = _create_mod(mods_root / "ModA", ABOUT_XML_V1)

    db_path = tmp_path / "metadata-cache.db"
    cache = MetadataCache(db_path)
    provider = LocalModProvider(mods_root, metadata_cache=cache)

    real_create_mod = base_module.create_listed_mod_from_path
    parse_call_count = 0

    def counting_create_mod(*args, **kwargs):
        nonlocal parse_call_count
        parse_call_count += 1
        return real_create_mod(*args, **kwargs)

    try:
        with patch.object(
            base_module, "create_listed_mod_from_path", side_effect=counting_create_mod
        ):
            discovered1 = await provider.discover("1.5")

        assert len(discovered1) == 1
        mod1 = discovered1[str(mod_a)]
        assert isinstance(mod1, AboutXmlMod)
        assert mod1.name == "Sample Mod One"
        assert str(mod1.package_id) == "author.samplemodone"
        assert mod1.provider_id == "local"
        assert parse_call_count == 1
        with patch.object(
            base_module, "create_listed_mod_from_path", side_effect=counting_create_mod
        ):
            discovered2 = await provider.discover("1.5")
        assert len(discovered2) == 1
        mod2 = discovered2[str(mod_a)]
        assert isinstance(mod2, AboutXmlMod)
        assert mod2.name == "Sample Mod One"
        assert str(mod2.package_id) == "author.samplemodone"
        assert mod2.provider_id == "local"
        assert mod2.mtime == mod1.mtime
        assert mod2.authors == ["TestAuthor"]
        assert mod2.supported_versions == {"1.4", "1.5"}
        assert mod2.description == "Initial description"
        assert mod2.mod_version == "1.0.0"
        assert mod2.url == "https://example.com/mod"
        assert mod2.mod_icon_path == Path("UI/Icon.png")
        assert "ludeon.rimworld" in mod2.about_rules.dependencies
        assert parse_call_count == 1
    finally:
        await cache.close()


@pytest.mark.asyncio
async def test_changed_metadata_reparses(tmp_path: Path) -> None:
    mods_root = tmp_path / "mods"
    mod_a = _create_mod(mods_root / "ModA", ABOUT_XML_V1)

    db_path = tmp_path / "metadata-cache.db"
    cache = MetadataCache(db_path)
    provider = LocalModProvider(mods_root, metadata_cache=cache)

    try:
        discovered1 = await provider.discover("1.5")
        mod1 = discovered1[str(mod_a)]
        assert mod1.name == "Sample Mod One"

        # Modify About.xml and bump mtime
        xml_path = mod_a / "About" / "About.xml"
        xml_path.write_text(ABOUT_XML_V2, encoding="utf-8")
        new_mtime = xml_path.stat().st_mtime + 5.0
        os.utime(xml_path, (new_mtime, new_mtime))

        discovered2 = await provider.discover("1.5")
        mod2 = discovered2[str(mod_a)]
        assert isinstance(mod2, AboutXmlMod)
        assert mod2.name == "Sample Mod One Updated"
        assert mod2.description == "Updated description"
        assert mod2.mtime == pytest.approx(new_mtime, abs=1e-3)
    finally:
        await cache.close()


@pytest.mark.asyncio
async def test_deleted_mods_are_invalidated(tmp_path: Path) -> None:
    mods_root = tmp_path / "mods"
    mod_a = _create_mod(mods_root / "ModA", ABOUT_XML_V1)
    mod_b = _create_mod(mods_root / "ModB", ABOUT_XML_TWO)

    db_path = tmp_path / "metadata-cache.db"
    cache = MetadataCache(db_path)

    cfg = AppConfig()
    cfg.paths.local = str(mods_root)
    ctx = CoreContext(cfg)

    provider = LocalModProvider(mods_root, metadata_cache=cache)
    mod_service = ModService(ctx, [provider], metadata_cache=cache)

    try:
        await mod_service.discover()
        assert len(ctx.all_mods) == 2
        assert str(mod_a) in ctx.all_mods
        assert str(mod_b) in ctx.all_mods

        # Verify DB has entries for both mods
        async with (
            aiosqlite.connect(str(db_path)) as conn,
            conn.execute("SELECT mod_path FROM mod_metadata") as cur,
        ):
            rows = await cur.fetchall()
        cached_paths = {r[0] for r in rows}
        assert normalize_path(mod_a) in cached_paths
        assert normalize_path(mod_b) in cached_paths

        # Delete ModB from disk
        for p in (mod_b / "About").iterdir():
            p.unlink()
        (mod_b / "About").rmdir()
        mod_b.rmdir()

        # Run discover again
        await mod_service.discover()
        assert len(ctx.all_mods) == 1
        assert str(mod_a) in ctx.all_mods
        assert str(mod_b) not in ctx.all_mods

        # Verify ModB was pruned from the database!
        async with (
            aiosqlite.connect(str(db_path)) as conn,
            conn.execute("SELECT mod_path FROM mod_metadata") as cur,
        ):
            rows_after = await cur.fetchall()
        cached_paths_after = {r[0] for r in rows_after}
        assert normalize_path(mod_a) in cached_paths_after
        assert normalize_path(mod_b) not in cached_paths_after
    finally:
        await mod_service.close()


@pytest.mark.asyncio
async def test_corrupt_cache_recovers_safely(tmp_path: Path) -> None:
    mods_root = tmp_path / "mods"
    mod_a = _create_mod(mods_root / "ModA", ABOUT_XML_V1)

    db_path = tmp_path / "metadata-cache.db"
    cache = MetadataCache(db_path)

    try:
        # Populate cache
        provider = LocalModProvider(mods_root, metadata_cache=cache)
        discovered1 = await provider.discover("1.5")
        assert len(discovered1) == 1

        # Close cache and corrupt the file
        await cache.close()
        db_path.write_bytes(b"corrupt non-sqlite data" * 50)

        # New cache instance pointing to corrupted file
        new_cache = MetadataCache(db_path)
        new_provider = LocalModProvider(mods_root, metadata_cache=new_cache)

        # Discovery must not crash; it recovers and parses mods
        discovered2 = await new_provider.discover("1.5")
        assert len(discovered2) == 1
        mod = discovered2[str(mod_a)]
        assert mod.name == "Sample Mod One"
        discovered3 = await new_provider.discover("1.5")
        assert len(discovered3) == 1
        assert discovered3[str(mod_a)].name == "Sample Mod One"
        await new_cache.close()
    finally:
        await cache.close()


@pytest.mark.asyncio
async def test_provider_distinctions_preserved(tmp_path: Path) -> None:
    local_root = tmp_path / "local"
    steam_root = tmp_path / "steam_cmd"

    local_mod = _create_mod(local_root / "LocalMod", ABOUT_XML_V1)
    steam_mod = _create_mod(steam_root / "SteamMod", ABOUT_XML_TWO, pfid="123456")

    db_path = tmp_path / "metadata-cache.db"
    cache = MetadataCache(db_path)

    local_prov = LocalModProvider(local_root, metadata_cache=cache)
    steam_prov = SteamCmdModProvider(steam_root, metadata_cache=cache)

    try:
        # Initial cold discovery
        local_mods = await local_prov.discover("1.5")
        steam_mods = await steam_prov.discover("1.5")

        assert len(local_mods) == 1
        assert len(steam_mods) == 1
        assert local_mods[str(local_mod)].provider_id == "local"
        assert steam_mods[str(steam_mod)].provider_id == "steam_cmd"

        # Second warm discovery
        local_mods_warm = await local_prov.discover("1.5")
        steam_mods_warm = await steam_prov.discover("1.5")

        assert len(local_mods_warm) == 1
        assert len(steam_mods_warm) == 1
        assert local_mods_warm[str(local_mod)].provider_id == "local"
        assert steam_mods_warm[str(steam_mod)].provider_id == "steam_cmd"
    finally:
        await cache.close()


@pytest.mark.asyncio
async def test_target_version_change_reparses(tmp_path: Path) -> None:
    mods_root = tmp_path / "mods"
    mod_dir = mods_root / "ModVersioned"
    about_xml = """<?xml version="1.0" encoding="utf-8"?>
<ModMetaData>
  <name>Versioned Mod</name>
  <author>Author</author>
  <packageId>Author.Versioned</packageId>
  <descriptionsByVersion>
    <v1.4>Description for 1.4</v1.4>
    <v1.5>Description for 1.5</v1.5>
  </descriptionsByVersion>
</ModMetaData>
"""
    _create_mod(mod_dir, about_xml)
    db_path = tmp_path / "metadata-cache.db"
    cache = MetadataCache(db_path)
    provider = LocalModProvider(mods_root, metadata_cache=cache)

    try:
        d14 = await provider.discover("1.4")
        assert d14[str(mod_dir)].description == "Description for 1.4"

        # Discovering for 1.5 misses cache because target_version differs
        d15 = await provider.discover("1.5")
        assert d15[str(mod_dir)].description == "Description for 1.5"
    finally:
        await cache.close()


@pytest.mark.asyncio
async def test_minimal_context_does_not_use_user_cache(tmp_path: Path) -> None:
    mods_root = tmp_path / "mods"
    _create_mod(mods_root / "ModA", ABOUT_XML_V1)
    cfg = AppConfig()
    ctx = CoreContext(cfg)
    assert not ctx.has_config_service
    provider = LocalModProvider(mods_root)
    mod_service = ModService(ctx, [provider])
    assert mod_service._metadata_cache is None
