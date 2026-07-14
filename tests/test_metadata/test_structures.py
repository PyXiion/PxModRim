from __future__ import annotations

from pxmodrim.core.models.metadata.structures import (
    AboutXmlMod,
    CaseInsensitiveSet,
    CaseInsensitiveStr,
    ListedMod,
)


class TestCaseInsensitiveStr:
    def test_lowercase_storage(self) -> None:
        pid = CaseInsensitiveStr("Test.Mod.PackageId")
        assert str(pid) == "test.mod.packageid"

    def test_equality(self) -> None:
        a = CaseInsensitiveStr("Test.Mod")
        b = CaseInsensitiveStr("test.mod")
        assert a == b

    def test_dict_key(self) -> None:
        d: dict[CaseInsensitiveStr, str] = {}
        d[CaseInsensitiveStr("Test.Mod")] = "value"
        assert d[CaseInsensitiveStr("test.mod")] == "value"


class TestCaseInsensitiveSet:
    def test_add_and_contains(self) -> None:
        s = CaseInsensitiveSet()
        s.add("Test.Mod")
        assert "test.mod" in s
        assert "Test.Mod" in s

    def test_discard(self) -> None:
        s = CaseInsensitiveSet(["Test.Mod"])
        s.discard("test.mod")
        assert len(s) == 0

    def test_union(self) -> None:
        a = CaseInsensitiveSet(["mod.a"])
        b = CaseInsensitiveSet(["Mod.B"])
        c = a | b
        assert "mod.a" in c
        assert "mod.b" in c

    def test_empty_eq(self) -> None:
        assert CaseInsensitiveSet() == CaseInsensitiveSet()


class TestListedMod:
    def test_default_creation(self) -> None:
        mod = ListedMod()
        assert mod.name == "Unknown Mod Name"
        assert mod.valid is True
        assert mod.provider_id == ""
        assert mod.mod_path is None

    def test_provider_id_assignable(self) -> None:
        mod = ListedMod()
        mod.provider_id = "local"
        assert mod.provider_id == "local"


class TestAboutXmlMod:
    def test_default_creation(self) -> None:
        mod = AboutXmlMod()
        assert mod.name == "Unknown Mod Name"
        assert mod.authors == []
        assert mod.mod_version == ""
        assert isinstance(mod.package_id, CaseInsensitiveStr)

    def test_dlc_name(self) -> None:
        mod = AboutXmlMod()
        mod.package_id = CaseInsensitiveStr("ludeon.rimworld.royalty")
        name = mod.get_dlc_name()
        assert name == "RimWorld - Royalty"
