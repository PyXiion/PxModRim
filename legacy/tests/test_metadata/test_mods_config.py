from __future__ import annotations

from pathlib import Path

import pytest

from pxmodrim.core import mods_config as mods_config_module
from pxmodrim.core.models.metadata.structures import CaseInsensitiveStr, ModsConfig
from pxmodrim.core.mods_config import (
    create_snapshot,
    get_snapshots,
    parse_mods_config,
    restore_snapshot,
    write_mods_config,
)

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
        write_mods_config(path, original, tmp_path / "snapshots")

        loaded = parse_mods_config(path)
        assert loaded is not None
        assert loaded.version == original.version
        assert loaded.activeMods == original.activeMods
        assert loaded.knownExpansions == original.knownExpansions

    def test_round_trip_preserves_unknown_xml(self, tmp_path: Path) -> None:
        path = tmp_path / "ModsConfig.xml"
        path.write_text(
            SAMPLE_XML.replace(
                "<ModsConfigData>",
                '<ModsConfigData custom="preserve">'
                '<otherSetting enabled="true"><value>42</value></otherSetting>',
            ),
            encoding="utf-8",
        )
        loaded = parse_mods_config(path)
        assert loaded is not None
        loaded.activeMods = [CaseInsensitiveStr("author.updated")]

        write_mods_config(path, loaded, tmp_path / "snapshots")

        root = mods_config_module.ET.parse(str(path)).getroot()
        assert root.get("custom") == "preserve"
        assert root.findtext("./otherSetting/value") == "42"
        assert root.find("./otherSetting").get("enabled") == "true"
        assert root.findtext("./activeMods/li") == "author.updated"


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


def _mods_config(package_id: str) -> ModsConfig:
    return ModsConfig(
        version="1.5",
        activeMods=[CaseInsensitiveStr(package_id)],
        knownExpansions=[],
    )


class TestModListSnapshotAndRollback:
    def test_replacing_config_preserves_previous_contents(self, tmp_path: Path) -> None:
        snapshots_dir = tmp_path / "snapshots"
        config_path = tmp_path / "ModsConfig.xml"

        write_mods_config(config_path, _mods_config("mod.alpha"), snapshots_dir)
        assert get_snapshots(snapshots_dir) == []

        write_mods_config(config_path, _mods_config("mod.beta"), snapshots_dir)

        [snapshot] = get_snapshots(snapshots_dir)
        previous = parse_mods_config(snapshot)
        current = parse_mods_config(config_path)
        assert previous is not None
        assert previous.activeMods == [CaseInsensitiveStr("mod.alpha")]
        assert current is not None
        assert current.activeMods == [CaseInsensitiveStr("mod.beta")]

    def test_replacement_retains_only_newest_ten_snapshots(
        self, tmp_path: Path
    ) -> None:
        snapshots_dir = tmp_path / "snapshots"
        config_path = tmp_path / "ModsConfig.xml"
        write_mods_config(config_path, _mods_config("mod.0"), snapshots_dir)

        for index in range(1, 12):
            write_mods_config(
                config_path,
                _mods_config(f"mod.{index}"),
                snapshots_dir,
            )

        snapshots = get_snapshots(snapshots_dir)
        retained_ids = []
        for snapshot in snapshots:
            parsed = parse_mods_config(snapshot)
            assert parsed is not None
            retained_ids.append(parsed.activeMods[0])

        assert len(snapshots) == 10
        assert retained_ids == [
            CaseInsensitiveStr(f"mod.{index}") for index in range(10, 0, -1)
        ]

    def test_configurable_retention_limit_honored(self, tmp_path: Path) -> None:
        snapshots_dir = tmp_path / "snapshots"
        config_path = tmp_path / "ModsConfig.xml"
        write_mods_config(
            config_path, _mods_config("mod.0"), snapshots_dir, max_snapshots=3
        )

        for index in range(1, 6):
            write_mods_config(
                config_path,
                _mods_config(f"mod.{index}"),
                snapshots_dir,
                max_snapshots=3,
            )

        snapshots = get_snapshots(snapshots_dir)
        assert len(snapshots) == 3
        retained_ids = [
            parsed.activeMods[0]
            for snapshot in snapshots
            if (parsed := parse_mods_config(snapshot)) is not None
        ]
        assert retained_ids == [
            CaseInsensitiveStr("mod.4"),
            CaseInsensitiveStr("mod.3"),
            CaseInsensitiveStr("mod.2"),
        ]

        # Lowering limit on duplicate content prunes excess snapshots before returning
        config_path.write_bytes(snapshots[0].read_bytes())
        duplicate = create_snapshot(config_path, snapshots_dir, max_snapshots=2)
        assert duplicate == snapshots[0]
        assert len(get_snapshots(snapshots_dir)) == 2

    def test_restoring_oldest_at_full_retention_preserves_snapshot_and_backup(
        self, tmp_path: Path
    ) -> None:
        snapshots_dir = tmp_path / "snapshots"
        config_path = tmp_path / "ModsConfig.xml"
        max_snapshots = 3
        write_mods_config(
            config_path,
            _mods_config("mod.0"),
            snapshots_dir,
            max_snapshots=max_snapshots,
        )
        for index in range(1, 4):
            write_mods_config(
                config_path,
                _mods_config(f"mod.{index}"),
                snapshots_dir,
                max_snapshots=max_snapshots,
            )

        selected_snapshot = get_snapshots(snapshots_dir)[-1]
        selected_data = selected_snapshot.read_bytes()

        assert (
            restore_snapshot(
                selected_snapshot,
                config_path,
                snapshots_dir,
                max_snapshots=max_snapshots,
            )
            is True
        )

        restored = parse_mods_config(config_path)
        assert restored is not None
        assert restored.activeMods == [CaseInsensitiveStr("mod.0")]
        assert selected_snapshot.exists()
        assert selected_snapshot.read_bytes() == selected_data

        snapshots = get_snapshots(snapshots_dir)
        retained_ids = []
        for snapshot in snapshots:
            parsed = parse_mods_config(snapshot)
            assert parsed is not None
            retained_ids.append(parsed.activeMods[0])

        assert len(snapshots) == max_snapshots
        assert retained_ids == [
            CaseInsensitiveStr("mod.3"),
            CaseInsensitiveStr("mod.2"),
            CaseInsensitiveStr("mod.0"),
        ]

    def test_restore_preserves_exact_snapshot_and_backs_up_current(
        self, tmp_path: Path
    ) -> None:
        snapshots_dir = tmp_path / "snapshots"
        config_path = tmp_path / "ModsConfig.xml"
        write_mods_config(config_path, _mods_config("mod.current"), snapshots_dir)

        snapshots_dir.mkdir(parents=True)
        snapshot_path = snapshots_dir / "ModsConfig_20260922_100000.xml"
        snapshot_data = (
            b"<?xml version='1.0' encoding='utf-8'?>\n"
            b"<ModsConfigData><version>1.5</version><activeMods>"
            b"<li>mod.saved</li></activeMods></ModsConfigData>\n"
        )
        snapshot_path.write_bytes(snapshot_data)

        assert restore_snapshot(snapshot_path, config_path, snapshots_dir) is True
        assert config_path.read_bytes() == snapshot_data

        backups = [
            path for path in get_snapshots(snapshots_dir) if path != snapshot_path
        ]
        assert len(backups) == 1
        backup = parse_mods_config(backups[0])
        assert backup is not None
        assert backup.activeMods == [CaseInsensitiveStr("mod.current")]

    def test_invalid_snapshot_never_changes_current_file(self, tmp_path: Path) -> None:
        snapshots_dir = tmp_path / "snapshots"
        config_path = tmp_path / "ModsConfig.xml"
        write_mods_config(config_path, _mods_config("mod.safe"), snapshots_dir)
        original = config_path.read_bytes()

        invalid_snapshots = [
            snapshots_dir / "ModsConfig_missing.xml",
            snapshots_dir / "ModsConfig_corrupt.xml",
            snapshots_dir / "ModsConfig_wrong_root.xml",
            snapshots_dir / "ModsConfig_missing_active_mods.xml",
        ]
        snapshots_dir.mkdir(parents=True, exist_ok=True)
        invalid_snapshots[1].write_text("not XML", encoding="utf-8")
        invalid_snapshots[2].write_text("<NotModsConfig/>", encoding="utf-8")
        invalid_snapshots[3].write_text(
            "<ModsConfigData><version>1.5</version></ModsConfigData>",
            encoding="utf-8",
        )

        for snapshot in invalid_snapshots:
            before = set(get_snapshots(snapshots_dir))
            assert restore_snapshot(snapshot, config_path, snapshots_dir) is False
            assert config_path.read_bytes() == original
            assert set(get_snapshots(snapshots_dir)) == before

    def test_failed_atomic_replace_leaves_current_file_intact(
        self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        snapshots_dir = tmp_path / "snapshots"
        config_path = tmp_path / "ModsConfig.xml"
        write_mods_config(config_path, _mods_config("mod.safe"), snapshots_dir)
        original = config_path.read_bytes()
        real_replace = mods_config_module.os.replace

        def fail_target_replace(source: Path, target: Path) -> None:
            if Path(target) == config_path:
                raise OSError("replace failed")
            real_replace(source, target)

        monkeypatch.setattr(mods_config_module.os, "replace", fail_target_replace)

        with pytest.raises(OSError, match="replace failed"):
            write_mods_config(config_path, _mods_config("mod.new"), snapshots_dir)

        assert config_path.read_bytes() == original
        assert not list(tmp_path.glob(".*.tmp"))
