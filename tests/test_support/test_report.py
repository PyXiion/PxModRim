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
    def test_redacts_home_dir_and_username(self) -> None:
        raw = "User alice at /home/alice/.config/pxmodrim/log.txt"
        result = redact(raw, home_dir="/home/alice", username="alice")
        assert result == "User <user> at ~/.config/pxmodrim/log.txt"

    def test_redacts_windows_style_paths(self) -> None:
        raw = r"Loaded C:\Users\Bob\AppData\Local\pxmodrim\test.log for Bob"
        result = redact(raw, home_dir=r"C:\Users\Bob", username="Bob")
        assert result == r"Loaded ~\AppData\Local\pxmodrim\test.log for <user>"

    def test_redacts_forward_slash_windows_path(self) -> None:
        raw = "Loaded C:/Users/Bob/AppData/test.log for Bob"
        result = redact(raw, home_dir=r"C:\Users\Bob", username="Bob")
        assert result == "Loaded ~/AppData/test.log for <user>"

    def test_preserves_text_when_no_user_match(self) -> None:
        raw = "General info: RimWorld 1.5 is running"
        result = redact(raw, home_dir="/home/other", username="other")
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

    def test_truncates_large_file_and_adds_banner(self, tmp_path: Path) -> None:
        log_file = tmp_path / "large.log"
        lines = [
            f"Log record number {i:04d} with some extra padding" for i in range(200)
        ]
        log_file.write_text("\n".join(lines), encoding="utf-8")

        # Cap to 200 bytes
        content = read_log_tail(log_file, max_bytes=200)
        assert "[... log truncated to last ~0 MB ...]" in content
        assert "Log record number 0199" in content
        assert "Log record number 0001" not in content


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
            "INFO | Application started for testuser\n", encoding="utf-8"
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
        assert "~/Games/RimWorld" in report
        assert "~/.config/unity3d" in report
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

        # Verify username is redacted in log tail
        assert "Application started for <user>" in report
        assert "testuser" not in report


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

    @pytest.mark.asyncio
    async def test_async_upload(self) -> None:
        uploader = PasteUploader()
        with patch.object(
            uploader, "upload", return_value="https://paste.rs/xyz"
        ) as mock_up:
            res = await uploader.async_upload("test content")
            assert res == "https://paste.rs/xyz"
            mock_up.assert_called_once_with("test content")
