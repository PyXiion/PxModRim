from __future__ import annotations

from pathlib import Path

import lxml.etree as ET
import pytest

from pxmodrim.core.models.metadata.parsing import (
    _match_byversion_raw,
    _match_versioned_child,
    create_about_mod,
    create_listed_mod_from_path,
    match_version,
    value_extractor,
)
from pxmodrim.core.models.metadata.structures import AboutXmlMod
from pxmodrim.core.utils import find_about_xml

TEST_DATA = Path(__file__).parent / "data"


class TestValueExtractor:
    def test_nested_dict(self) -> None:
        assert value_extractor({"key": "value"}) == "value"

    def test_ignore_if_no_matching_field(self) -> None:
        result = value_extractor(
            {"@IgnoreIfNoMatchingField": "True", "#text": "actual"}
        )
        assert result == "actual"

    def test_none(self) -> None:
        assert value_extractor(None) is None


class TestCreateAboutMod:
    def test_basic_mod(self) -> None:
        data = {
            "name": "Test Mod",
            "author": "Tester",
            "packageId": "test.mod",
            "description": "A test mod.",
            "supportedVersions": {"li": "1.5"},
        }
        valid, mod = create_about_mod(data, "1.5")
        assert valid is True
        assert mod.name == "Test Mod"
        assert mod.authors == ["Tester"]
        assert str(mod.package_id) == "test.mod"
        assert mod.supported_versions == {"1.5"}

    def test_missing_package_id_is_invalid(self) -> None:
        valid, mod = create_about_mod({"name": "No ID Mod"}, "1.5")
        assert valid is False
        assert mod.valid is False
        assert str(mod.package_id) == "missing.packageid"

    def test_empty_package_id_is_invalid(self) -> None:
        valid, mod = create_about_mod({"packageId": ""}, "1.5")
        assert valid is False
        assert mod.valid is False

    def test_version_keys_must_match_exactly(self) -> None:
        assert match_version({"v1.50": "wrong", "v1x5-beta": "wrong"}, "1.5") == (
            False,
            None,
        )
        assert _match_byversion_raw(
            {"v1.50": "wrong", "v1x5-beta": "wrong"}, "1.5"
        ) == (False, None)
        parent = ET.Element("versions")
        parent.extend([ET.Element("v1.50"), ET.Element("v1x5-beta")])
        assert _match_versioned_child(parent, "1.5") is None

    def test_unprefixed_exact_version_key_is_supported(self) -> None:
        assert match_version({"1.5": "right"}, "1.5") == (True, "right")

    def test_missing_package_id_in_xml_is_invalid(self, tmp_path: Path) -> None:
        about_dir = tmp_path / "About"
        about_dir.mkdir()
        about_path = about_dir / "About.xml"
        about_path.write_text(
            "<ModMetaData><name>No ID Mod</name></ModMetaData>", encoding="utf-8"
        )
        valid, mod = create_listed_mod_from_path(
            tmp_path, "1.5", about_xml_path=about_path
        )
        assert valid is False
        assert isinstance(mod, AboutXmlMod)
        assert mod.valid is False
        assert str(mod.package_id) == "missing.packageid"

    def test_dlc_mod(self) -> None:
        data = {"packageId": "ludeon.rimworld.royalty"}
        valid, mod = create_about_mod(data, "1.5")
        assert valid is True
        assert mod.name == "RimWorld - Royalty"
        assert mod.steam_app_id == 1149640


class TestFindAboutXml:
    def test_valid_mod(self) -> None:
        about_path = find_about_xml(TEST_DATA / "valid_mod")
        assert about_path is not None
        assert about_path.name == "About.xml"

    def test_nonexistent(self) -> None:
        result = find_about_xml(TEST_DATA / "nonexistent")
        assert result is None


class TestCreateListedModFromPath:
    def test_valid_mod(self) -> None:
        mod_path = TEST_DATA / "valid_mod"
        valid, mod = create_listed_mod_from_path(mod_path, "1.5")
        assert valid is True
        assert isinstance(mod, AboutXmlMod)
        assert mod.name == "Test Mod"
        assert mod.authors == ["TestAuthor"]
        assert str(mod.package_id) == "test.mod"
        assert mod.mod_path == mod_path
        assert mod.provider_id == ""

    def test_invalid_path(self) -> None:
        with pytest.raises(ValueError, match="Path must be a directory"):
            create_listed_mod_from_path(
                TEST_DATA / "valid_mod" / "About" / "About.xml",
                "1.5",
            )

    def test_malformed_about_xml(self, tmp_path: Path) -> None:
        about_dir = tmp_path / "About"
        about_dir.mkdir()
        (about_dir / "About.xml").write_text("<ModMetaData>", encoding="utf-8")

        valid, mod = create_listed_mod_from_path(tmp_path, "1.5")

        assert valid is False
        assert isinstance(mod, AboutXmlMod)
        assert mod.valid is False
        assert mod.mod_path == tmp_path
        assert mod.uuid == str(tmp_path)
