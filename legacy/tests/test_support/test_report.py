from __future__ import annotations

import io
import urllib.error
from pathlib import Path
from typing import Any
from unittest.mock import MagicMock, patch

import pytest

from pxmodrim.core.config import AppConfig, PathConfig
from pxmodrim.core.models.metadata.structures import AboutXmlMod
from pxmodrim.core.support.report import (
    PasteUploader,
    ReportUploadError,
    build_report,
    get_app_version,
    read_log_tail,
    redact,
)


class TestRedact:
    def test_redacts_home_dir_and_username_in_user_paths(self) -> None:
        raw = "User alice at /home/alice/.config/log.txt and /mnt/home/alice/x"
        result = redact(raw, home_dir="/home/alice", username="alice")
        assert result == "User alice at ~/.config/log.txt and /mnt/home/<user>/x"

    def test_redacts_windows_style_paths(self) -> None:
        raw = r"Loaded C:\Users\Bob\AppData\Local\pxmodrim\test.log"
        result = redact(raw, home_dir=r"C:\Users\Bob", username="Bob")
        assert result == r"Loaded ~\AppData\Local\pxmodrim\test.log"

    def test_redacts_forward_slash_windows_path(self) -> None:
        raw = "Loaded C:/Users/Bob/AppData/test.log"
        result = redact(raw, home_dir=r"C:\Users\Bob", username="Bob")
        assert result == "Loaded ~/AppData/test.log"

    def test_home_dir_respects_path_boundary(self) -> None:
        raw = "/home/bob/a and /home/bobby/b and /home/bob"
        result = redact(raw, home_dir="/home/bob", username="bob")
        assert result == "~/a and /home/bobby/b and ~"

    def test_redacts_username_on_removable_media_mounts(self) -> None:
        raw = "/run/media/alice/SD/mods and /media/alice/x and /media/alicia/y"
        result = redact(raw, home_dir="/home/alice", username="alice")
        assert (
            result
            == "/run/media/<user>/SD/mods and /media/<user>/x and /media/alicia/y"
        )

    def test_redacts_doubled_backslashes_from_repr(self) -> None:
        raw = repr(r"C:\Users\Bob\AppData") + " and " + repr(r"D:\Users\Bob\mods")
        result = redact(raw, home_dir=r"C:\Users\Bob", username="Bob")
        assert "Bob" not in result
        assert result.startswith("'~")

    def test_short_username_redacted_in_other_user_paths(self) -> None:
        raw = r"ed saw D:\Users\ed\mods and /Users/ED/x but not /Users/edgar"
        result = redact(raw, home_dir="/home/ed", username="ed")
        expected = (
            r"ed saw D:\Users\<user>\mods and /Users/<user>/x but not /Users/edgar"
        )
        assert result == expected

    def test_common_word_username_does_not_corrupt_package_ids(self) -> None:
        raw = "Core (ludeon.rimworld) from /opt/rimworld; rimworld started"
        result = redact(raw, home_dir="/home/rimworld", username="rimworld")
        assert result == raw


class TestReadLogTail:
    def test_missing_file_returns_notice(self, tmp_path: Path) -> None:
        missing = tmp_path / "does_not_exist.log"
        content = read_log_tail(missing)
        assert "<no log file found>" in content

    def test_empty_file_returns_notice(self, tmp_path: Path) -> None:
        empty = tmp_path / "empty.log"
        empty.touch()
        content = read_log_tail(empty)
        assert "<empty log file>" in content

    def test_reads_small_file_completely(self, tmp_path: Path) -> None:
        log_file = tmp_path / "app.log"
        text = "Line 1\nLine 2\nLine 3\n"
        log_file.write_text(text, encoding="utf-8")
        content = read_log_tail(log_file)
        assert content == text

    def test_parses_loguru_serialized_json_lines(self, tmp_path: Path) -> None:
        log_file = tmp_path / "json.log"
        raw_json = (
            '{"text": "2026-09-28 | INFO | Loaded mods\\n", "record": {"id": 1}}\n'
            "Plain log line\n"
            '{"text": "2026-09-28 | INFO | Ready\\n", "record": {"id": 2}}\n'
        )
        log_file.write_text(raw_json, encoding="utf-8")
        content = read_log_tail(log_file)
        expected = (
            "2026-09-28 | INFO | Loaded mods\n"
            "Plain log line\n"
            "2026-09-28 | INFO | Ready\n"
        )
        assert content == expected
        assert '{"text"' not in content

    def test_truncates_large_file_and_adds_banner(self, tmp_path: Path) -> None:
        log_file = tmp_path / "large.log"
        lines = [
            f"Log record number {i:04d} with some extra padding" for i in range(200)
        ]
        log_file.write_text("\n".join(lines), encoding="utf-8")

        # Cap to 200 bytes
        content = read_log_tail(log_file, max_bytes=200)
        assert "[... truncated, showing last " in content
        assert " of " in content
        assert "Log record number 0199" in content
        assert "Log record number 0001" not in content


def test_get_app_version_fallback_unknown() -> None:
    from importlib.metadata import PackageNotFoundError

    with patch(
        "pxmodrim.core.support.report.version", side_effect=PackageNotFoundError
    ):
        assert get_app_version() == "unknown"


class MockContext:
    def __init__(
        self,
        game_version: str = "1.5.4243",
        paths: PathConfig | None = None,
        mods: dict[str, Any] | None = None,
        active_uuids: list[str] | None = None,
    ) -> None:
        self.game_version = game_version
        self.config = AppConfig(paths=paths or PathConfig())
        self.all_mods = mods or {}
        self.active_uuids = active_uuids or []


class TestBuildReport:
    def test_report_contains_all_sections_and_active_mods_in_order(
        self, tmp_path: Path
    ) -> None:
        mod1 = AboutXmlMod()
        mod1.name = "Harmony"
        mod1.package_id = "brrainz.harmony"  # type: ignore[assignment]

        mod2 = AboutXmlMod()
        mod2.name = "Core"
        mod2.package_id = "ludeon.rimworld"  # type: ignore[assignment]

        mod3 = AboutXmlMod()
        mod3.name = "Royalty"
        mod3.package_id = "ludeon.rimworld.royalty"  # type: ignore[assignment]

        mods = {"u1": mod1, "u2": mod2, "u3": mod3}
        # Specify load order: u2 (Core), then u1 (Harmony), then u3 (Royalty)
        active = ["u2", "u1", "u3"]

        home = tmp_path / "home" / "testuser"
        home.mkdir(parents=True, exist_ok=True)
        paths = PathConfig(
            game=str(home / "Games" / "RimWorld"),
            config_folder=str(home / ".config" / "unity3d"),
            local=str(home / "Games" / "RimWorld" / "Mods"),
        )
        ctx = MockContext(
            game_version="1.5.4243",
            paths=paths,
            mods=mods,
            active_uuids=active,
        )
        log_path = tmp_path / "app.log"

        log_path.write_text(
            "INFO | Loaded /home/testuser/mods for testuser\n", encoding="utf-8"
        )

        report = build_report(
            ctx,
            log_path=log_path,
            home_dir=str(home),
            username="testuser",
        )

        # Verify sections
        assert "=== PxModRim System Report ===" in report
        assert "=== RimWorld Configuration ===" in report
        assert "=== Mod Collection ===" in report
        assert "=== Active Mods (Load Order) ===" in report
        assert "=== Application Log ===" in report

        # Verify system info
        assert f"App Version: {get_app_version()}" in report
        assert "Python Version:" in report
        assert "OS:" in report

        # Verify game version and paths are redacted
        assert "Game Version: 1.5.4243" in report
        assert "~/Games/RimWorld" in report.replace("\\", "/")
        assert "~/.config/unity3d" in report.replace("\\", "/")
        assert str(home) not in report

        # Verify mod counts
        assert "Total Mods: 3" in report
        assert "Active Mods: 3" in report

        # Verify active mods in load order
        expected_order = (
            "  1. Core (ludeon.rimworld)\n"
            "  2. Harmony (brrainz.harmony)\n"
            "  3. Royalty (ludeon.rimworld.royalty)"
        )
        assert expected_order in report

        # Username is redacted only in user-directory paths
        assert "Loaded /home/<user>/mods for testuser" in report


class TestPasteUploader:
    def test_upload_success(self) -> None:
        uploader = PasteUploader(endpoint="https://paste.rs/test")
        mock_response = MagicMock()
        mock_response.status = 201
        mock_response.read.return_value = b"https://paste.rs/abc123\n"
        mock_response.__enter__.return_value = mock_response
        mock_response.__exit__.return_value = None
        with patch(
            "urllib.request.urlopen", return_value=mock_response
        ) as mock_urlopen:
            url = uploader.upload("Report text payload")
            assert url == "https://paste.rs/abc123"

            mock_urlopen.assert_called_once()
            req = mock_urlopen.call_args[0][0]
            assert req.get_header("User-agent").startswith("PxModRim/")
            assert req.get_header("Content-type") == "text/plain; charset=utf-8"
            assert req.data == b"Report text payload"

    def test_upload_empty_text_raises_error(self) -> None:
        uploader = PasteUploader()
        with pytest.raises(ReportUploadError, match="Report text is empty"):
            uploader.upload("   ")

    def test_upload_non_2xx_status_raises_error(self) -> None:
        uploader = PasteUploader()
        mock_response = MagicMock()
        mock_response.status = 500
        mock_response.__enter__.return_value = mock_response
        mock_response.__exit__.return_value = None
        with (
            patch("urllib.request.urlopen", return_value=mock_response),
            pytest.raises(ReportUploadError, match="HTTP status 500"),
        ):
            uploader.upload("Report content")

    def test_upload_http_error_raises_report_upload_error(self) -> None:
        uploader = PasteUploader()
        http_error = urllib.error.HTTPError(
            url="https://paste.rs/",
            code=403,
            msg="Forbidden",
            hdrs={},  # type: ignore[arg-type]
            fp=io.BytesIO(b""),
        )
        with (
            patch("urllib.request.urlopen", side_effect=http_error),
            pytest.raises(ReportUploadError, match="HTTP 403 Forbidden"),
        ):
            uploader.upload("Report content")

    def test_upload_non_url_response_raises_error(self) -> None:
        uploader = PasteUploader()
        mock_response = MagicMock()
        mock_response.status = 200
        mock_response.read.return_value = b"Something unexpected went wrong"
        mock_response.__enter__.return_value = mock_response
        mock_response.__exit__.return_value = None
        with (
            patch("urllib.request.urlopen", return_value=mock_response),
            pytest.raises(ReportUploadError, match="valid URL"),
        ):
            uploader.upload("Report content")

    def test_upload_network_error_raises_error(self) -> None:
        uploader = PasteUploader()
        url_error = urllib.error.URLError("Connection refused")
        with (
            patch("urllib.request.urlopen", side_effect=url_error),
            pytest.raises(ReportUploadError, match="network error"),
        ):
            uploader.upload("Report content")

    def test_upload_timeout_raises_error(self) -> None:
        uploader = PasteUploader()
        with (
            patch("urllib.request.urlopen", side_effect=TimeoutError()),
            pytest.raises(ReportUploadError, match="timed out"),
        ):
            uploader.upload("Report content")
