from __future__ import annotations

from pathlib import Path

from pxmodrim.core.models.metadata.structures import CaseInsensitiveStr, ModsConfig
from pxmodrim.core.mods_config import parse_mods_config, write_mods_config

SAMPLE_XML = """<?xml version="1.0" encoding="utf-8"?>
<ModsConfigData>
  <version>1.6.4871 rev600</version>
  <activeMods>
    <li>ludeon.rimworld</li>
    <li>ludeon.rimworld.royalty</li>
    <li>ludeon.rimworld.ideology</li>
    <li>ludeon.rimworld.biotech</li>
    <li>ludeon.rimworld.anomaly</li>
    <li>ludeon.rimworld.odyssey</li>
  </activeMods>
  <knownExpansions>
    <li>ludeon.rimworld.royalty</li>
    <li>ludeon.rimworld.ideology</li>
    <li>ludeon.rimworld.biotech</li>
    <li>ludeon.rimworld.anomaly</li>
    <li>ludeon.rimworld.odyssey</li>
  </knownExpansions>
</ModsConfigData>
"""


def _parse_str(content: str, tmp_path: Path) -> ModsConfig | None:
    path = tmp_path / "ModsConfig.xml"
    path.write_text(content, encoding="utf-8")
    return parse_mods_config(path)


class TestParseModsConfig:
    def test_parse_full(self, tmp_path: Path) -> None:
        config = _parse_str(SAMPLE_XML, tmp_path)
        assert config is not None
        assert config.version == "1.6.4871 rev600"
        assert config.activeMods == [
            CaseInsensitiveStr("ludeon.rimworld"),
            CaseInsensitiveStr("ludeon.rimworld.royalty"),
            CaseInsensitiveStr("ludeon.rimworld.ideology"),
            CaseInsensitiveStr("ludeon.rimworld.biotech"),
            CaseInsensitiveStr("ludeon.rimworld.anomaly"),
            CaseInsensitiveStr("ludeon.rimworld.odyssey"),
        ]
        assert config.knownExpansions == [
            CaseInsensitiveStr("ludeon.rimworld.royalty"),
            CaseInsensitiveStr("ludeon.rimworld.ideology"),
            CaseInsensitiveStr("ludeon.rimworld.biotech"),
            CaseInsensitiveStr("ludeon.rimworld.anomaly"),
            CaseInsensitiveStr("ludeon.rimworld.odyssey"),
        ]

    def test_parse_single_active_mod(self, tmp_path: Path) -> None:
        xml = """<?xml version="1.0" encoding="utf-8"?>
<ModsConfigData>
  <version>1.5</version>
  <activeMods>
    <li>ludeon.rimworld</li>
  </activeMods>
  <knownExpansions>
    <li>ludeon.rimworld.royalty</li>
  </knownExpansions>
</ModsConfigData>"""
        config = _parse_str(xml, tmp_path)
        assert config is not None
        assert config.activeMods == [CaseInsensitiveStr("ludeon.rimworld")]
        assert config.knownExpansions == [CaseInsensitiveStr("ludeon.rimworld.royalty")]

    def test_parse_empty_config(self, tmp_path: Path) -> None:
        xml = """<?xml version="1.0" encoding="utf-8"?>
<ModsConfigData>
  <version>1.5</version>
  <activeMods/>
</ModsConfigData>"""
        config = _parse_str(xml, tmp_path)
        assert config is not None
        assert config.activeMods == []
        assert len(config.knownExpansions) > 0

    def test_parse_missing_file(self) -> None:
        result = parse_mods_config(Path("/nonexistent/ModsConfig.xml"))
        assert result is None


class TestModsConfigToDict:
    def test_to_dict_structure_with_li(self) -> None:
        config = ModsConfig(
            version="1.5",
            activeMods=[CaseInsensitiveStr("ludeon.rimworld")],
            knownExpansions=[CaseInsensitiveStr("ludeon.rimworld.royalty")],
        )
        d = config.to_dict()
        assert d == {
            "version": "1.5",
            "activeMods": {"li": ["ludeon.rimworld"]},
            "knownExpansions": {"li": ["ludeon.rimworld.royalty"]},
        }

    def test_to_dict_empty_lists(self) -> None:
        config = ModsConfig(
            version="1.5",
            activeMods=[],
            knownExpansions=[],
        )
        d = config.to_dict()
        assert d["activeMods"] == {"li": []}
        assert d["knownExpansions"] == {"li": []}


class TestModsConfigRoundTrip:
    def test_round_trip(self, tmp_path: Path) -> None:
        original = ModsConfig(
            version="1.6.4871 rev600",
            activeMods=[
                CaseInsensitiveStr("ludeon.rimworld"),
                CaseInsensitiveStr("ludeon.rimworld.royalty"),
                CaseInsensitiveStr("ludeon.rimworld.ideology"),
                CaseInsensitiveStr("ludeon.rimworld.biotech"),
                CaseInsensitiveStr("ludeon.rimworld.anomaly"),
            ],
            knownExpansions=[
                CaseInsensitiveStr("ludeon.rimworld.royalty"),
                CaseInsensitiveStr("ludeon.rimworld.ideology"),
                CaseInsensitiveStr("ludeon.rimworld.biotech"),
                CaseInsensitiveStr("ludeon.rimworld.anomaly"),
            ],
        )
        path = tmp_path / "ModsConfig.xml"
        write_mods_config(path, original)

        loaded = parse_mods_config(path)
        assert loaded is not None
        assert loaded.version == original.version
        assert loaded.activeMods == original.activeMods
        assert loaded.knownExpansions == original.knownExpansions

class TestModsConfigProperties:
    def test_active_mods_getter_returns_copy(self) -> None:
        config = ModsConfig(
            version="1.5",
            activeMods=[CaseInsensitiveStr("ludeon.rimworld")],
            knownExpansions=[],
        )
        mods = config.activeMods
        mods.append(CaseInsensitiveStr("test.extra"))
        assert config.activeMods == [CaseInsensitiveStr("ludeon.rimworld")]

    def test_known_expansions_getter_returns_copy(self) -> None:
        config = ModsConfig(
            version="1.5",
            activeMods=[],
            knownExpansions=[CaseInsensitiveStr("ludeon.rimworld.royalty")],
        )
        exps = config.knownExpansions
        exps.clear()
        assert config.knownExpansions == [CaseInsensitiveStr("ludeon.rimworld.royalty")]

    def test_setter_normalizes_case(self) -> None:
        config = ModsConfig(
            version="1.5",
            activeMods=[],
            knownExpansions=[],
        )
        config.activeMods = ["Test.UPPERCASE.Mod"]
        assert config.activeMods == [CaseInsensitiveStr("test.uppercase.mod")]

    def test_clear_active_mods(self) -> None:
        config = ModsConfig(
            version="1.5",
            activeMods=[CaseInsensitiveStr("ludeon.rimworld")],
            knownExpansions=[CaseInsensitiveStr("ludeon.rimworld.royalty")],
        )
        config.clear_active_mods()
        assert config.activeMods == []
        assert config.knownExpansions == [CaseInsensitiveStr("ludeon.rimworld.royalty")]

    def test_clear_all(self) -> None:
        config = ModsConfig(
            version="1.5",
            activeMods=[CaseInsensitiveStr("ludeon.rimworld")],
            knownExpansions=[CaseInsensitiveStr("ludeon.rimworld.royalty")],
        )
        config.clear_all()
        assert config.activeMods == []
        assert config.knownExpansions == []

    def test_check_active_duplicates_true(self) -> None:
        config = ModsConfig(
            version="1.5",
            activeMods=[
                CaseInsensitiveStr("mod.a"),
                CaseInsensitiveStr("Mod.A"),
            ],
            knownExpansions=[],
        )
        assert config.check_active_duplicates() is True

    def test_check_active_duplicates_false(self) -> None:
        config = ModsConfig(
            version="1.5",
            activeMods=[
                CaseInsensitiveStr("mod.a"),
                CaseInsensitiveStr("mod.b"),
            ],
            knownExpansions=[],
        )
        assert config.check_active_duplicates() is False

    def test_check_expansions_duplicates_true(self) -> None:
        config = ModsConfig(
            version="1.5",
            activeMods=[],
            knownExpansions=[
                CaseInsensitiveStr("ludeon.rimworld.royalty"),
                CaseInsensitiveStr("Ludeon.RimWorld.Royalty"),
            ],
        )
        assert config.check_expansions_duplicates() is True

    def test_check_expansions_duplicates_false(self) -> None:
        config = ModsConfig(
            version="1.5",
            activeMods=[],
            knownExpansions=[
                CaseInsensitiveStr("ludeon.rimworld.royalty"),
                CaseInsensitiveStr("ludeon.rimworld.ideology"),
            ],
        )
        assert config.check_expansions_duplicates() is False
