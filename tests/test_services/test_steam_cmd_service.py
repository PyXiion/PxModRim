from __future__ import annotations

import asyncio
import shutil
import sys
import threading
from contextlib import nullcontext
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, PropertyMock, patch

import pytest
from PySide6.QtWidgets import QApplication
from pytest import raises as assert_raises
from qasync import QEventLoop

from pxmodrim.core.config import AppConfig, ConfigService
from pxmodrim.core.context import CoreContext
from pxmodrim.core.services.steam_cmd_service import (
    STEAMCMD_BATCH_SIZE,
    SteamCmdItemStatus,
    SteamCmdResult,
    SteamCmdService,
    SymlinkConflictError,
)
from pxmodrim.core.services.steam_cmd_worker import SteamCmdDownloadWorker


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
def ctx(tmp_path: Path) -> CoreContext:
    cfg = AppConfig()
    cfg.paths.steamcmd_prefix = str(tmp_path / "scmd")
    return CoreContext(cfg, ConfigService(tmp_path))


@pytest.fixture
def service(ctx: CoreContext) -> SteamCmdService:
    svc = SteamCmdService()
    svc.setup(ctx)
    return svc


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
def ctx_with_local(tmp_path: Path) -> CoreContext:
    cfg = AppConfig()
    cfg.paths.steamcmd_prefix = str(tmp_path / "scmd")
    cfg.paths.local = str(tmp_path / "local")
    return CoreContext(cfg, ConfigService(tmp_path))


@pytest.fixture
def service_local(ctx_with_local: CoreContext) -> SteamCmdService:
    svc = SteamCmdService()
    svc.setup(ctx_with_local)
    return svc


class TestEnsureSymlink:
    def test_creates_link(self, service_local: SteamCmdService, tmp_path: Path) -> None:
        asyncio.run(service_local.ensure_symlink(str(tmp_path / "local")))
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
            asyncio.run(
                service_local.ensure_symlink(str(tmp_path / "local"), forced=False)
            )

    def test_real_dir_with_force_overwrites(
        self, service_local: SteamCmdService, tmp_path: Path
    ) -> None:
        dst = Path(service_local.symlink_target)
        dst.mkdir(parents=True)
        (dst / "keep.txt").write_text("data")
        asyncio.run(service_local.ensure_symlink(str(tmp_path / "local"), forced=True))
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
        asyncio.run(service_local.ensure_symlink(str(tmp_path / "local")))
        assert dst.resolve() == (tmp_path / "local").resolve()

    def test_empty_local_raises(self, service: SteamCmdService) -> None:
        with pytest.raises(SymlinkConflictError):
            asyncio.run(service.ensure_symlink(target=""))

    def test_replaces_existing_file_without_force_raises(
        self, service_local: SteamCmdService, tmp_path: Path
    ) -> None:
        dst = Path(service_local.symlink_target)
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_text("not a dir")
        with pytest.raises(SymlinkConflictError):
            asyncio.run(
                service_local.ensure_symlink(str(tmp_path / "local"), forced=False)
            )

    def test_replaces_existing_file_with_force(
        self, service_local: SteamCmdService, tmp_path: Path
    ) -> None:
        dst = Path(service_local.symlink_target)
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_text("not a dir")
        asyncio.run(service_local.ensure_symlink(str(tmp_path / "local"), forced=True))
        assert dst.is_symlink()
        assert dst.resolve() == (tmp_path / "local").resolve()


class TestConfigDerivedPaths:
    def test_reflects_config_prefix(
        self, service: SteamCmdService, tmp_path: Path
    ) -> None:
        assert service._ctx is not None
        ctx = service._ctx
        prefix = str(tmp_path / "custom_prefix")
        ctx.config.paths.steamcmd_prefix = prefix
        assert service.prefix == prefix
        assert service.install_path == str(Path(prefix) / "steamcmd")
        assert service.steam_path == str(Path(prefix) / "steam")
        assert service.content_path.endswith(
            str(Path("steamapps", "workshop", "content"))
        )
        exe = "steamcmd.exe" if sys.platform == "win32" else "steamcmd.sh"
        assert service.executable == str(Path(prefix) / "steamcmd" / exe)

    def test_ensure_installed_saves_new_prefix(
        self, service: SteamCmdService, tmp_path: Path
    ) -> None:
        prefix = str(tmp_path / "prefix_for_install")
        exe_name = "steamcmd.exe" if sys.platform == "win32" else "steamcmd.sh"
        executable = Path(prefix) / "steamcmd" / exe_name
        executable.parent.mkdir(parents=True, exist_ok=True)
        executable.write_text("#!/bin/sh\n")
        assert asyncio.run(service.ensure_installed(prefix=prefix)) is True
        assert service.prefix == prefix

    def test_empty_falls_back_to_config_dir(self, service: SteamCmdService) -> None:
        assert service._ctx is not None
        service._ctx.config.paths.steamcmd_prefix = ""
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
        ctx = service._ctx
        assert ctx is not None
        assert ctx.config.paths.steamcmd_prefix == new_prefix
        cfg = ctx.config_service.load("config.json", AppConfig)
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


class TestDownloadWorker:
    def test_runscript_uses_separate_argv_entries(self, tmp_path: Path) -> None:
        script = tmp_path / "download.txt"
        script.write_text("quit\n")
        process = MagicMock()
        process.stdout = []
        process.returncode = 0
        process.poll.return_value = 0
        worker = SteamCmdDownloadWorker(
            "steamcmd",
            str(tmp_path),
            [["111"]],
            lambda _batch: str(script),
        )

        with patch(
            "pxmodrim.core.services.steam_cmd_worker.subprocess.Popen",
            return_value=process,
        ) as popen:
            worker._run_batch(["111"], [], [], 1)

        assert popen.call_args.args[0] == ["steamcmd", "+runscript", str(script)]
        assert not script.exists()

    def test_cancel_terminates_silent_process(self, tmp_path: Path) -> None:
        script = tmp_path / "download.txt"
        script.write_text("quit\n")
        spawned = threading.Event()
        terminated = threading.Event()
        process = MagicMock()
        process.returncode = None
        process.poll.side_effect = lambda: process.returncode
        process.wait.side_effect = lambda: terminated.wait(1)

        class SilentOutput:
            def __iter__(self):
                return self

            def __next__(self) -> str:
                terminated.wait()
                raise StopIteration

        process.stdout = SilentOutput()
        worker = SteamCmdDownloadWorker(
            "steamcmd",
            str(tmp_path),
            [["111"]],
            lambda _batch: str(script),
        )
        errors: list[Exception] = []

        def spawn(*_args, **_kwargs):
            spawned.set()
            return process

        def terminate(child) -> None:
            child.returncode = -15
            terminated.set()

        def run_batch() -> None:
            try:
                worker._run_batch(["111"], [], [], 1)
            except Exception as error:
                errors.append(error)

        with (
            patch(
                "pxmodrim.core.services.steam_cmd_worker.subprocess.Popen",
                side_effect=spawn,
            ),
            patch(
                "pxmodrim.core.services.steam_cmd_worker._kill",
                side_effect=terminate,
            ) as kill,
        ):
            thread = threading.Thread(target=run_batch)
            thread.start()
            assert spawned.wait(1)
            worker.cancel()
            assert terminated.wait(1)
            thread.join(1)

        assert not thread.is_alive()
        assert errors == []
        kill.assert_called_once_with(process)
        assert not script.exists()

    def test_cancelled_process_exit_is_not_reported_as_failure(
        self, tmp_path: Path
    ) -> None:
        script = tmp_path / "download.txt"
        script.write_text("quit\n")
        process = MagicMock()
        process.returncode = -15
        process.poll.return_value = -15
        worker = SteamCmdDownloadWorker(
            "steamcmd",
            str(tmp_path),
            [["111"]],
            lambda _batch: str(script),
        )

        class CancelledOutput:
            def __iter__(self):
                return self

            def __next__(self) -> str:
                worker.cancel()
                raise StopIteration

        process.stdout = CancelledOutput()
        statuses: list[str] = []
        worker.status.connect(statuses.append)

        with (
            patch(
                "pxmodrim.core.services.steam_cmd_worker.subprocess.Popen",
                return_value=process,
            ),
            patch("pxmodrim.core.services.steam_cmd_worker._kill"),
        ):
            worker._run_batch(["111"], [], [], 1)

        assert statuses == []

    def test_task_cancellation_cancels_worker_before_async_wait(
        self, service: SteamCmdService
    ) -> None:
        worker = MagicMock()
        calls: list[str] = []
        wait_started = threading.Event()
        release_wait = threading.Event()
        worker.start.side_effect = lambda: calls.append("start")
        worker.cancel.side_effect = lambda: calls.append("cancel")
        worker.quit.side_effect = lambda: calls.append("quit")

        def wait() -> bool:
            calls.append("wait")
            wait_started.set()
            return release_wait.wait(1)

        worker.wait.side_effect = wait
        service._runner_factory = lambda *_args: worker

        async def cancel_download() -> None:
            task = asyncio.create_task(
                service.download_mods(["111"], validate=False)
            )
            await asyncio.sleep(0)
            task.cancel()
            for _ in range(100):
                if wait_started.is_set():
                    break
                await asyncio.sleep(0.01)
            assert wait_started.is_set()
            assert calls[:3] == ["start", "cancel", "wait"]
            release_wait.set()
            with pytest.raises(asyncio.CancelledError):
                await task

        with patch.object(service, "is_installed", return_value=True):
            asyncio.run(cancel_download())

        assert calls == ["start", "cancel", "wait", "quit"]
        assert service._worker is None


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
        with assert_raises(ValueError, match="No mods selected for download"):
            asyncio.run(service.download_mods([], validate=False))

    def test_noop_when_not_installed(
        self, service: SteamCmdService, tmp_path: Path
    ) -> None:
        with assert_raises(ValueError, match="not installed"):
            asyncio.run(service.download_mods(["111"], validate=False))

    def test_parses_output(
        self,
        service: SteamCmdService,
        fake_steamcmd: Path,
        qapp: QApplication,
        tmp_path: Path,
    ) -> None:
        assert service._ctx is not None
        service._ctx.config.paths.steamcmd_prefix = str(tmp_path)
        Path(service.executable).parent.mkdir(parents=True, exist_ok=True)
        if sys.platform == "win32":
            _exec_path = Path(service.executable).with_suffix(".cmd")
            shutil.copy2(str(fake_steamcmd), _exec_path)
            _exe_mock = patch.object(
                SteamCmdService,
                "executable",
                new_callable=PropertyMock,
                return_value=str(_exec_path),
            )
        else:
            shutil.copy2(str(fake_steamcmd), service.executable)
            Path(service.executable).chmod(0o755)
            _exe_mock = nullcontext()

        statuses: list[SteamCmdItemStatus] = []
        result: list[SteamCmdResult] = []
        service.download_item_status_changed.connect(statuses.append)
        service.download_finished.connect(result.append)
        with _exe_mock:
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
        assert service._ctx is not None
        service._ctx.config.paths.steamcmd_prefix = str(tmp_path)
        Path(service.executable).parent.mkdir(parents=True, exist_ok=True)
        if sys.platform == "win32":
            _exec_path = Path(service.executable).with_suffix(".cmd")
            shutil.copy2(str(fake_steamcmd), _exec_path)
            _exe_mock = patch.object(
                SteamCmdService,
                "executable",
                new_callable=PropertyMock,
                return_value=str(_exec_path),
            )
        else:
            shutil.copy2(str(fake_steamcmd), service.executable)
            Path(service.executable).chmod(0o755)
            _exe_mock = nullcontext()

        result: list[SteamCmdResult] = []
        service.download_finished.connect(result.append)

        def _cancel_soon() -> None:
            service.cancel()

        timer = threading.Timer(0.1, _cancel_soon)
        timer.start()
        with _exe_mock:
            run_async(
                service.download_mods(["111", "222"], validate=False),
                qapp,
            )
        timer.cancel()

        assert result
        assert "111" not in result[0].succeeded
