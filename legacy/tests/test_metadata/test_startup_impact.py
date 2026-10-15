from __future__ import annotations

from pathlib import Path

import pytest

from pxmodrim.core.services.startup_impact_service import (
    StartupImpactReport,
    format_impact,
    get_startup_impact_path,
    normalize_package_id,
    parse_startup_impact,
)


def _write_json(path: Path, content: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return path


_SAMPLE_JSON = """{
  "timestamp": "2026-07-16T14:30:45.123Z",
  "loadingTime": 42500.5,
  "metrics": {
    "LoadModXml": 10200.0,
    "ResolveReferences": 1500.0
  },
  "totalImpact": 0.0,
  "offThreadMetrics": {},
  "offThreadTotalImpact": 0.0,
  "mods": [
    {
      "modName": "Harmony",
      "modPackageId": "brrainz.harmony",
      "metrics": {
        "ModConstructor": 1000.0
      },
      "totalImpact": 1230.5,
      "offThreadMetrics": {
        "LoadPatches": 300.0
      },
      "offThreadTotalImpact": 300.0
    },
    {
      "modName": "Some Mod",
      "modPackageId": "author.somemod",
      "metrics": {
        "LoadModXml": 4500.0
      },
      "totalImpact": 4500.0,
      "offThreadMetrics": {},
      "offThreadTotalImpact": 0.0
    },
    {
      "modName": "Fast Mod",
      "modPackageId": "author.fast",
      "metrics": {},
      "totalImpact": 50.0,
      "offThreadMetrics": {},
      "offThreadTotalImpact": 0.0
    },
    {
      "modName": "No Package ID Mod",
      "totalImpact": 200.0,
      "metrics": {},
      "offThreadMetrics": {},
      "offThreadTotalImpact": 0.0
    }
  ]
}
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
        assert result == Path("/home/user/StartupImpactData.json")

    def test_accepts_str(self) -> None:
        result = get_startup_impact_path("/home/user/Config")
        assert result == Path("/home/user/StartupImpactData.json")


class TestParseStartupImpact:
    def test_parse_full_report(self, tmp_path: Path) -> None:
        path = _write_json(tmp_path / "StartupImpactData.json", _SAMPLE_JSON)
        report = parse_startup_impact(path)
        assert report is not None
        assert isinstance(report, StartupImpactReport)
        assert report.loading_time_s == 42.5005
        assert report.timestamp == "2026-07-16T14:30:45.123Z"
        assert report.metrics == {"LoadModXml": 10.2, "ResolveReferences": 1.5}
        assert report.total_impact == 0.0
        assert len(report.mods) == 4

    def test_base_game_time_from_metrics(self, tmp_path: Path) -> None:
        path = _write_json(tmp_path / "StartupImpactData.json", _SAMPLE_JSON)
        report = parse_startup_impact(path)
        assert report is not None
        # Base game time is the sum of the base-game profiler metrics, not a
        # subtraction of loading time minus mod impacts.
        assert report.base_game_time == pytest.approx(11.7)
        # loading_time is the true wall-clock total and is NOT equal to the
        # sum of mod on-thread impacts (there is a real unaccounted "Others").
        assert report.loading_time_s != pytest.approx(report.total_time)

    def test_mods_total_includes_off_thread(self, tmp_path: Path) -> None:
        path = _write_json(tmp_path / "StartupImpactData.json", _SAMPLE_JSON)
        report = parse_startup_impact(path)
        assert report is not None
        harmony = report.mods[0]
        assert harmony.total_impact_s == 1.2305
        assert harmony.off_thread_total_impact_s == 0.3
        # total_time counts only on-thread mod impact; mods_total_time adds
        # off-thread impact.
        assert report.total_time == pytest.approx(1.2305 + 4.5 + 0.05 + 0.2)
        assert report.off_thread_total_time == pytest.approx(0.3)
        assert report.mods_total_time == pytest.approx(report.total_time + 0.3)

    def test_mod_values(self, tmp_path: Path) -> None:
        path = _write_json(tmp_path / "StartupImpactData.json", _SAMPLE_JSON)
        report = parse_startup_impact(path)
        assert report is not None

        harmony = report.mods[0]
        assert harmony.mod_name == "Harmony"
        assert harmony.package_id == "brrainz.harmony"
        assert harmony.total_impact_s == 1.2305
        assert harmony.metrics == {"ModConstructor": 1.0}

        some_mod = report.mods[1]
        assert some_mod.mod_name == "Some Mod"
        assert some_mod.package_id == "author.somemod"
        assert some_mod.total_impact_s == 4.5
        assert some_mod.metrics == {"LoadModXml": 4.5}

        fast = report.mods[2]
        assert fast.mod_name == "Fast Mod"
        assert fast.package_id == "author.fast"
        assert fast.total_impact_s == 0.05
        assert fast.metrics == {}

    def test_missing_package_id(self, tmp_path: Path) -> None:
        path = _write_json(tmp_path / "StartupImpactData.json", _SAMPLE_JSON)
        report = parse_startup_impact(path)
        assert report is not None

        no_pid = report.mods[3]
        assert no_pid.mod_name == "No Package ID Mod"
        assert no_pid.package_id is None
        assert no_pid.total_impact_s == 0.2
        assert no_pid.metrics == {}

    def test_file_not_found(self, tmp_path: Path) -> None:
        report = parse_startup_impact(tmp_path / "nonexistent.json")
        assert report is None

    def test_empty_mods(self, tmp_path: Path) -> None:
        data = """{"loadingTime": 1000.0, "mods": []}"""
        path = _write_json(tmp_path / "empty_mods.json", data)
        report = parse_startup_impact(path)
        assert report is not None
        assert report.loading_time_s == 1.0
        assert len(report.mods) == 0

    def test_not_a_dict(self, tmp_path: Path) -> None:
        path = _write_json(tmp_path / "not_a_dict.json", "[]")
        report = parse_startup_impact(path)
        assert report is None

    def test_malformed_json(self, tmp_path: Path) -> None:
        path = _write_json(tmp_path / "bad.json", "{invalid")
        report = parse_startup_impact(path)
        assert report is None

    def test_invalid_json_structure(self, tmp_path: Path) -> None:
        data = """{"wrongKey": "value"}"""
        path = _write_json(tmp_path / "invalid.json", data)
        report = parse_startup_impact(path)
        assert report is not None
        assert len(report.mods) == 0

    def test_total_time(self, tmp_path: Path) -> None:
        data = """{
            "loadingTime": 50000.0,
            "mods": [
                {"modName": "A", "modPackageId": "a.a",
                 "totalImpact": 2000.0, "metrics": {},
                 "offThreadMetrics": {}, "offThreadTotalImpact": 0.0},
                {"modName": "B", "modPackageId": "b.b",
                 "totalImpact": 3000.0, "metrics": {},
                 "offThreadMetrics": {}, "offThreadTotalImpact": 0.0}
            ]
        }"""
        r = parse_startup_impact(_write_json(tmp_path / "total.json", data))
        assert r is not None
        assert r.total_time == pytest.approx(5.0)

    def test_total_time_empty(self, tmp_path: Path) -> None:
        data = """{"loadingTime": 1000.0, "mods": []}"""
        r = parse_startup_impact(_write_json(tmp_path / "total_empty.json", data))
        assert r is not None
        assert r.total_time == 0.0

    def test_mod_off_thread_fields(self, tmp_path: Path) -> None:
        data = """{
            "loadingTime": 10000.0,
            "mods": [
                {
                    "modName": "Test Mod",
                    "modPackageId": "author.test",
                    "totalImpact": 2000.0,
                    "metrics": {"A": 1000.0, "B": 1000.0},
                    "offThreadMetrics": {"C": 500.0},
                    "offThreadTotalImpact": 500.0
                }
            ]
        }"""
        path = _write_json(tmp_path / "offthread.json", data)
        report = parse_startup_impact(path)
        assert report is not None
        mod = report.mods[0]
        assert mod.off_thread_metrics == {"C": 0.5}
        assert mod.off_thread_total_impact_s == 0.5
