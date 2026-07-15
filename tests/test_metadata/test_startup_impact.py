from __future__ import annotations

from pathlib import Path

from pxmodrim.core.services.startup_impact_service import (
    StartupImpactReport,
    format_impact,
    get_startup_impact_path,
    normalize_package_id,
    parse_startup_impact,
)


def _write_xml(path: Path, content: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return path


_SAMPLE_XML = """<?xml version="1.0" encoding="utf-8"?>
<StartupImpactSession>
  <sessionData>
    <loadingTime>42500.5</loadingTime>
    <mods>
      <li>
        <modName>Harmony</modName>
        <modPackageId>brrainz.harmony</modPackageId>
        <totalImpact>1230.5</totalImpact>
      </li>
      <li>
        <modName>Some Mod</modName>
        <modPackageId>author.somemod</modPackageId>
        <totalImpact>4500.0</totalImpact>
      </li>
      <li>
        <modName>Fast Mod</modName>
        <modPackageId>author.fast</modPackageId>
        <totalImpact>50.0</totalImpact>
      </li>
      <li>
        <modName>No Package ID Mod</modName>
        <totalImpact>200.0</totalImpact>
      </li>
    </mods>
  </sessionData>
</StartupImpactSession>
"""


class TestNormalizePackageId:
    def test_lowercase(self) -> None:
        assert normalize_package_id("Author.Mod") == "author.mod"

    def test_steam_suffix(self) -> None:
        assert normalize_package_id("author.mod_steam") == "author.mod"

    def test_already_normalized(self) -> None:
        assert normalize_package_id("author.mod") == "author.mod"


class TestFormatImpact:
    def test_seconds_to_ms(self) -> None:
        assert format_impact(1.234) == "1234ms"

    def test_fractional(self) -> None:
        assert format_impact(0.05) == "50ms"

    def test_zero(self) -> None:
        assert format_impact(0.0) == "0ms"


class TestGetStartupImpactPath:
    def test_derives_from_config_parent(self) -> None:
        result = get_startup_impact_path(Path("/home/user/Config"))
        assert result == Path("/home/user/StartupImpactData.xml")

    def test_accepts_str(self) -> None:
        result = get_startup_impact_path("/home/user/Config")
        assert result == Path("/home/user/StartupImpactData.xml")


class TestParseStartupImpact:
    def test_parse_full_report(self, tmp_path: Path) -> None:
        path = _write_xml(tmp_path / "StartupImpactData.xml", _SAMPLE_XML)
        report = parse_startup_impact(path)
        assert report is not None
        assert isinstance(report, StartupImpactReport)
        assert report.loading_time_s == 42.5005
        assert len(report.mods) == 4

    def test_mod_values(self, tmp_path: Path) -> None:
        path = _write_xml(tmp_path / "StartupImpactData.xml", _SAMPLE_XML)
        report = parse_startup_impact(path)
        assert report is not None

        harmony = report.mods[0]
        assert harmony.mod_name == "Harmony"
        assert harmony.package_id == "brrainz.harmony"
        assert harmony.total_impact_s == 1.2305

        some_mod = report.mods[1]
        assert some_mod.mod_name == "Some Mod"
        assert some_mod.package_id == "author.somemod"
        assert some_mod.total_impact_s == 4.5

        fast = report.mods[2]
        assert fast.mod_name == "Fast Mod"
        assert fast.package_id == "author.fast"
        assert fast.total_impact_s == 0.05

    def test_missing_package_id(self, tmp_path: Path) -> None:
        path = _write_xml(tmp_path / "StartupImpactData.xml", _SAMPLE_XML)
        report = parse_startup_impact(path)
        assert report is not None

        no_pid = report.mods[3]
        assert no_pid.mod_name == "No Package ID Mod"
        assert no_pid.package_id is None
        assert no_pid.total_impact_s == 0.2

    def test_file_not_found(self, tmp_path: Path) -> None:
        report = parse_startup_impact(tmp_path / "nonexistent.xml")
        assert report is None

    def test_empty_file(self, tmp_path: Path) -> None:
        path = _write_xml(tmp_path / "empty.xml", "<?xml version=\"1.0\"?><root/>")
        report = parse_startup_impact(path)
        assert report is None

    def test_empty_mods(self, tmp_path: Path) -> None:
        xml = """<?xml version="1.0" encoding="utf-8"?>
<StartupImpactSession>
  <sessionData>
    <loadingTime>1000.0</loadingTime>
    <mods/>
  </sessionData>
</StartupImpactSession>"""
        path = _write_xml(tmp_path / "empty_mods.xml", xml)
        report = parse_startup_impact(path)
        assert report is not None
        assert report.loading_time_s == 1.0
        assert len(report.mods) == 0

    def test_single_mod_not_list(self, tmp_path: Path) -> None:
        """When there's only one <li>, xml_path_to_json returns a dict not list."""
        xml = """<?xml version="1.0" encoding="utf-8"?>
<StartupImpactSession>
  <sessionData>
    <loadingTime>500.0</loadingTime>
    <mods>
      <li>
        <modName>Single Mod</modName>
        <modPackageId>author.single</modPackageId>
        <totalImpact>100.0</totalImpact>
      </li>
    </mods>
  </sessionData>
</StartupImpactSession>"""
        path = _write_xml(tmp_path / "single.xml", xml)
        report = parse_startup_impact(path)
        assert report is not None
        assert len(report.mods) == 1
        assert report.mods[0].mod_name == "Single Mod"
        assert report.mods[0].total_impact_s == 0.1

    def test_no_session_data(self, tmp_path: Path) -> None:
        xml = """<?xml version="1.0" encoding="utf-8"?>
<StartupImpactSession>
  <wrongKey>123</wrongKey>
</StartupImpactSession>"""
        path = _write_xml(tmp_path / "no_session.xml", xml)
        report = parse_startup_impact(path)
        assert report is None
