from __future__ import annotations

from pathlib import Path

import pytest

from pxmodrim._compat.utils import find_about_xml
from pxmodrim.models.metadata.parsing import (
    create_about_mod,
    create_listed_mod_from_path,
    value_extractor,
)
from pxmodrim.models.metadata.structures import AboutXmlMod

TEST_DATA = Path(__file__).parent / "data"


class TestValueExtractor:
    def test_string(self) -> None:
        assert value_extractor("hello") == "hello"

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

    def test_missing_package_id(self) -> None:
        data = {"name": "No ID Mod"}
        valid, mod = create_about_mod(data, "1.5")
        assert valid is True
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
