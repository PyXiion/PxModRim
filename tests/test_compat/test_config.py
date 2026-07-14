from __future__ import annotations

import sys
from pathlib import Path

import pytest

from pxmodrim._compat.config import read_game_version


class TestReadGameVersion:
    def test_parses_file(self, tmp_path: Path) -> None:
        ver_file = tmp_path / "Version.txt"
        ver_file.write_text("1.6.4871 rev598\n", encoding="utf-8")
        result = read_game_version(tmp_path)
        assert result == "1.6.4871 rev598"

    def test_strips_whitespace(self, tmp_path: Path) -> None:
        ver_file = tmp_path / "Version.txt"
        ver_file.write_text("  1.6.4871 rev598  \n", encoding="utf-8")
        result = read_game_version(tmp_path)
        assert result == "1.6.4871 rev598"

    def test_missing_file_returns_none(self, tmp_path: Path) -> None:
        result = read_game_version(tmp_path)
        assert result is None

    def test_empty_game_path_returns_none(self) -> None:
        result = read_game_version("")
        assert result is None

    def test_nonexistent_path_returns_none(self) -> None:
        result = read_game_version("/nonexistent/path")
        assert result is None

    @pytest.mark.skipif(
        sys.platform == "win32", reason="chmod has no effect on Windows"
    )
    def test_unreadable_file_returns_none(self, tmp_path: Path) -> None:
        ver_file = tmp_path / "Version.txt"
        ver_file.write_text("1.6.4871 rev598\n")
        ver_file.chmod(0o000)
        try:
            result = read_game_version(tmp_path)
            assert result is None
        finally:
            ver_file.chmod(0o644)
