from __future__ import annotations

import asyncio
import sys
from pathlib import Path
from unittest.mock import AsyncMock, patch

import msgspec
import pytest
from PySide6.QtWidgets import QApplication
from qasync import QEventLoop

from pxmodrim.core.config import AppConfig, ConfigService
from pxmodrim.core.context import CoreContext
from pxmodrim.core.msgspec_hooks import dec_hook
from pxmodrim.core.services.steam_cmd_service import (
    STEAMCMD_BATCH_SIZE,
    SteamCmdItemStatus,
    SteamCmdResult,
    SteamCmdService,
    SymlinkConflictError,
)


@pytest.fixture
def cfg_path(tmp_path: Path) -> Path:
    return tmp_path / "config.json"


def run_async(coro, app: QApplication):
    """Run a coroutine on a qasync event loop (needed for Qt signal delivery)."""
    import contextlib

    loop = QEventLoop(app)
    try:
        return loop.run_until_complete(coro)
    finally:
        with contextlib.suppress(Exception):
            loop.close()


@pytest.fixture(scope="module")
def qapp():
    app = QApplication.instance()
    if app is None:
        app = QApplication([])
    yield app


@pytest.fixture
def ctx(tmp_path: Path, cfg_path: Path) -> CoreContext:
    cfg = AppConfig()
    cfg.paths.steamcmd_prefix = str(tmp_path / "scmd")
    ctx = CoreContext(cfg)
    ctx._config_service = ConfigService(cfg, cfg_path)
    return ctx


@pytest.fixture
def service(ctx: CoreContext, cfg_path: Path) -> SteamCmdService:
    return SteamCmdService(ctx, ConfigService(ctx.config, cfg_path))


class TestPaths:
    def test_exe_name_linux(self, service: SteamCmdService) -> None:
        if sys.platform == "win32":
            assert service.executable.endswith("steamcmd.exe")
        else:
            assert service.executable.endswith("steamcmd.sh")

    def test_install_and_steam_paths(self, service: SteamCmdService) -> None:
        assert service.install_path.endswith("steamcmd")
        assert service.steam_path.endswith("steam")
        assert service.content_path.endswith(
            str(Path("steamapps", "workshop", "content"))
        )

    def test_symlink_target(self, service: SteamCmdService) -> None:
        assert service.symlink_target.endswith(
            str(Path("steamapps", "workshop", "content", "294100"))
        )

    def test_fallback_prefix_when_empty(self, tmp_path: Path) -> None:
        cfg = AppConfig()
        cfg.paths.steamcmd_prefix = ""
        ctx = CoreContext(cfg)
        svc = SteamCmdService(ctx, ConfigService(cfg, tmp_path / "config.json"))
        assert svc.prefix
        assert svc.prefix.endswith("steamcmd")


class TestIsInstalled:
    def test_false_when_missing(self, service: SteamCmdService) -> None:
        assert service.is_installed() is False

    def test_true_when_exe_present(self, service: SteamCmdService) -> None:
        Path(service.executable).parent.mkdir(parents=True, exist_ok=True)
        Path(service.executable).write_text("#!/bin/sh\n")
        assert service.is_installed() is True


class TestBuildDownloadScript:
    def test_basic_script(self, service: SteamCmdService, tmp_path: Path) -> None:
        with patch(
            "tempfile.gettempdir",
            return_value=str(tmp_path),
        ):
            script = service._build_download_script(["111", "222"])
        text = Path(script).read_text(encoding="utf-8")
        assert "force_install_dir" in text
        assert "login anonymous" in text
        assert "workshop_download_item 294100 111" in text
        assert "workshop_download_item 294100 222" in text
        assert "quit" in text
        assert "validate" not in text

    def test_validate_flag(self, service: SteamCmdService, tmp_path: Path) -> None:
        with patch(
            "tempfile.gettempdir",
            return_value=str(tmp_path),
        ):
            script = service._build_download_script(["111"], validate=True)
        text = Path(script).read_text(encoding="utf-8")
        assert "workshop_download_item 294100 111 validate" in text


class TestSplitBatches:
    def test_exact_multiple(self) -> None:
        ids = [str(i) for i in range(STEAMCMD_BATCH_SIZE * 2)]
        batches = SteamCmdService.split_batches(ids)
        assert len(batches) == 2
        assert all(len(b) == STEAMCMD_BATCH_SIZE for b in batches)

    def test_remainder(self) -> None:
        ids = [str(i) for i in range(STEAMCMD_BATCH_SIZE + 3)]
        batches = SteamCmdService.split_batches(ids)
        assert len(batches) == 2
        assert len(batches[0]) == STEAMCMD_BATCH_SIZE
        assert len(batches[1]) == 3

    def test_empty(self) -> None:
        assert SteamCmdService.split_batches([]) == []


@pytest.fixture
def ctx_with_local(tmp_path: Path, cfg_path: Path) -> CoreContext:
    cfg = AppConfig()
    cfg.paths.steamcmd_prefix = str(tmp_path / "scmd")
    cfg.paths.local = str(tmp_path / "local")
    ctx = CoreContext(cfg)
    ctx._config_service = ConfigService(cfg, cfg_path)
    return ctx


@pytest.fixture
def service_local(ctx_with_local: CoreContext, cfg_path: Path) -> SteamCmdService:
    return SteamCmdService(
        ctx_with_local,
        ConfigService(ctx_with_local.config, cfg_path),
    )


class TestEnsureSymlink:
    def test_creates_link(self, service_local: SteamCmdService, tmp_path: Path) -> None:
        asyncio.run(service_local.ensure_symlink())
        dst = Path(service_local.symlink_target)
        assert dst.is_symlink()
        assert dst.resolve() == (tmp_path / "local").resolve()

    def test_real_dir_without_force_raises(
        self, service_local: SteamCmdService, tmp_path: Path
    ) -> None:
        dst = Path(service_local.symlink_target)
        dst.mkdir(parents=True)
        (dst / "keep.txt").write_text("data")
        with pytest.raises(SymlinkConflictError):
            asyncio.run(service_local.ensure_symlink(forced=False))

    def test_real_dir_with_force_overwrites(
        self, service_local: SteamCmdService, tmp_path: Path
    ) -> None:
        dst = Path(service_local.symlink_target)
        dst.mkdir(parents=True)
        (dst / "keep.txt").write_text("data")
        asyncio.run(service_local.ensure_symlink(forced=True))
        assert dst.is_symlink()
        assert dst.resolve() == (tmp_path / "local").resolve()

    def test_replaces_existing_link(
        self, service_local: SteamCmdService, tmp_path: Path
    ) -> None:
        dst = Path(service_local.symlink_target)
        dst.parent.mkdir(parents=True, exist_ok=True)
        other = tmp_path / "other"
        other.mkdir()
        dst.symlink_to(other, target_is_directory=True)
        asyncio.run(service_local.ensure_symlink())
        assert dst.resolve() == (tmp_path / "local").resolve()

    def test_empty_local_raises(self, service: SteamCmdService) -> None:
        with pytest.raises(SymlinkConflictError):
            asyncio.run(service.ensure_symlink())

    def test_explicit_target(self, service: SteamCmdService, tmp_path: Path) -> None:
        target = tmp_path / "custom_local"
        target.mkdir()
        asyncio.run(service.ensure_symlink(target))
        dst = Path(service.symlink_target)
        assert dst.is_symlink()
        assert dst.resolve() == target.resolve()

    def test_replaces_existing_file_without_force_raises(
        self, service_local: SteamCmdService, tmp_path: Path
    ) -> None:
        dst = Path(service_local.symlink_target)
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_text("not a dir")
        with pytest.raises(SymlinkConflictError):
            asyncio.run(service_local.ensure_symlink(forced=False))

    def test_replaces_existing_file_with_force(
        self, service_local: SteamCmdService, tmp_path: Path
    ) -> None:
        dst = Path(service_local.symlink_target)
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_text("not a dir")
        asyncio.run(service_local.ensure_symlink(forced=True))
        assert dst.is_symlink()
        assert dst.resolve() == (tmp_path / "local").resolve()


class TestSetPrefix:
    def test_recomputes_derived_paths(
        self, service: SteamCmdService, tmp_path: Path
    ) -> None:
        new_prefix = str(tmp_path / "new_steamcmd")
        service.set_prefix(new_prefix)
        assert service.prefix == new_prefix
        assert service.install_path == str(Path(new_prefix) / "steamcmd")
        assert service.steam_path == str(Path(new_prefix) / "steam")
        assert service.content_path.endswith(
            str(Path("steamapps", "workshop", "content"))
        )
        exe = "steamcmd.exe" if sys.platform == "win32" else "steamcmd.sh"
        assert service.executable == str(Path(new_prefix) / "steamcmd" / exe)

    def test_ensure_installed_uses_new_prefix(
        self, service: SteamCmdService, tmp_path: Path
    ) -> None:
        new_prefix = str(tmp_path / "prefix_for_install")
        service.set_prefix(new_prefix)
        Path(service.executable).parent.mkdir(parents=True, exist_ok=True)
        Path(service.executable).write_text("#!/bin/sh\n")
        assert asyncio.run(service.ensure_installed()) is True
        assert service.prefix == new_prefix

    def test_empty_falls_back_to_config_dir(self, service: SteamCmdService) -> None:
        service.set_prefix("")
        assert service.prefix
        assert service.prefix.endswith("steamcmd")
        is_windows_exe = service.executable.endswith("steamcmd.exe")
        assert service.executable.endswith("steamcmd.sh") or is_windows_exe


class TestEnsureInstalled:
    def test_unsupported_platform(self, service: SteamCmdService) -> None:
        with patch(
            "pxmodrim.core.services.steam_cmd_service.platform.system",
            return_value="FreeBSD",
        ):
            assert asyncio.run(service.ensure_installed()) is False

    def test_persists_prefix(self, service: SteamCmdService, tmp_path: Path) -> None:
        new_prefix = str(tmp_path / "new_prefix")

        def _fake_extract(data, url, dest):
            Path(service.executable).parent.mkdir(parents=True, exist_ok=True)
            Path(service.executable).write_text("#!/bin/sh\n")

        with (
            patch(
                "pxmodrim.core.services.steam_cmd_service.platform.system",
                return_value="Linux",
            ),
            patch(
                "pxmodrim.core.services.steam_cmd_service._download_bytes",
                new=AsyncMock(return_value=b""),
            ),
            patch(
                "pxmodrim.core.services.steam_cmd_service._extract_archive",
                side_effect=_fake_extract,
            ),
        ):
            result = asyncio.run(service.ensure_installed(prefix=new_prefix))
        assert result is True
        assert service.prefix == new_prefix
        assert service._ctx.config.paths.steamcmd_prefix == new_prefix
        cfg = msgspec.json.decode(
            Path(service._config.path).read_bytes(),
            type=AppConfig,
            dec_hook=dec_hook,
        )
        assert cfg.paths.steamcmd_prefix == new_prefix

    def test_installs_when_missing(
        self, service: SteamCmdService, tmp_path: Path
    ) -> None:
        def _fake_extract(data, url, dest):
            Path(service.executable).parent.mkdir(parents=True, exist_ok=True)
            Path(service.executable).write_text("#!/bin/sh\n")

        with (
            patch(
                "pxmodrim.core.services.steam_cmd_service.platform.system",
                return_value="Linux",
            ),
            patch(
                "pxmodrim.core.services.steam_cmd_service._download_bytes",
                new=AsyncMock(return_value=b""),
            ),
            patch(
                "pxmodrim.core.services.steam_cmd_service._extract_archive",
                side_effect=_fake_extract,
            ),
        ):
            result = asyncio.run(service.ensure_installed())
        assert result is True

    def test_no_reinstall_when_present(
        self, service: SteamCmdService, tmp_path: Path
    ) -> None:
        Path(service.executable).parent.mkdir(parents=True, exist_ok=True)
        Path(service.executable).write_text("#!/bin/sh\n")
        with (
            patch(
                "pxmodrim.core.services.steam_cmd_service._download_bytes",
                new=AsyncMock(),
            ) as dl,
            patch("pxmodrim.core.services.steam_cmd_service._extract_archive") as ex,
        ):
            result = asyncio.run(service.ensure_installed())
        assert result is True
        dl.assert_not_called()
        ex.assert_not_called()

    def test_download_failure_returns_false(self, service: SteamCmdService) -> None:
        with (
            patch(
                "pxmodrim.core.services.steam_cmd_service.platform.system",
                return_value="Linux",
            ),
            patch(
                "pxmodrim.core.services.steam_cmd_service._download_bytes",
                new=AsyncMock(side_effect=RuntimeError("boom")),
            ),
        ):
            result = asyncio.run(service.ensure_installed())
        assert result is False


@pytest.fixture
def fake_steamcmd(tmp_path: Path) -> Path:
    """A fake 'steamcmd' that prints RimSort-style output for ids 111/222/333."""
    if sys.platform == "win32":
        script = tmp_path / "steamcmd.bat"
        script.write_text(
            "@echo off\n"
            "echo Downloading item 111...\n"
            "ping -n 2 127.0.0.1 > nul\n"
            "echo Success. Downloaded item 111\n"
            "echo Downloading item 222...\n"
            "ping -n 2 127.0.0.1 > nul\n"
            "echo Success. Downloaded item 222\n"
            "echo Downloading item 333...\n"
            "ping -n 2 127.0.0.1 > nul\n"
            "echo ERROR! Download item 333\n"
        )
    else:
        script = tmp_path / "steamcmd.sh"
        script.write_text(
            "#!/bin/sh\n"
            "echo 'Downloading item 111...'\n"
            "sleep 0.3\n"
            "echo 'Success. Downloaded item 111'\n"
            "echo 'Downloading item 222...'\n"
            "sleep 0.3\n"
            "echo 'Success. Downloaded item 222'\n"
            "echo 'Downloading item 333...'\n"
            "sleep 0.3\n"
            "echo 'ERROR! Download item 333'\n"
        )
        script.chmod(0o755)
    return script


class TestDownloadMods:
    def test_noop_when_empty(self, service: SteamCmdService, tmp_path: Path) -> None:
        seen: list[str] = []
        service.status_message_changed.connect(seen.append)
        asyncio.run(service.download_mods([], validate=False))
        assert any("No mods selected" in s for s in seen)

    def test_noop_when_not_installed(
        self, service: SteamCmdService, tmp_path: Path
    ) -> None:
        seen: list[str] = []
        service.status_message_changed.connect(seen.append)
        asyncio.run(service.download_mods(["111"], validate=False))
        assert any("not installed" in s for s in seen)

    def test_parses_output(
        self,
        service: SteamCmdService,
        fake_steamcmd: Path,
        qapp: QApplication,
        tmp_path: Path,
    ) -> None:
        service._executable = str(fake_steamcmd)

        statuses: list[SteamCmdItemStatus] = []
        result: list[SteamCmdResult] = []
        service.download_item_status_changed.connect(statuses.append)
        service.download_finished.connect(result.append)
        run_async(
            service.download_mods(["111", "222", "333"], validate=False),
            qapp,
        )

        assert result and set(result[0].succeeded) == {"111", "222"}
        assert result[0].failed == ["333"]
        assert any(s.mod_id == "111" and s.status == "success" for s in statuses)
        assert any(s.mod_id == "333" and s.status == "error" for s in statuses)

    def test_cancel_stops_early(
        self,
        service: SteamCmdService,
        fake_steamcmd: Path,
        qapp: QApplication,
        tmp_path: Path,
    ) -> None:
        import threading

        service._executable = str(fake_steamcmd)

        result: list[SteamCmdResult] = []
        service.download_finished.connect(result.append)

        def _cancel_soon() -> None:
            service.cancel()

        timer = threading.Timer(0.1, _cancel_soon)
        timer.start()
        run_async(
            service.download_mods(["111", "222"], validate=False),
            qapp,
        )
        timer.cancel()

        assert result
        assert "111" not in result[0].succeeded
