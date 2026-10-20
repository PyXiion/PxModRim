from __future__ import annotations

import asyncio
import sys
import time
from pathlib import Path
from types import SimpleNamespace
from typing import Any, cast

import pytest
from PySide6.QtGui import QCloseEvent
from PySide6.QtWidgets import QApplication

from pxmodrim import _app as app_module
from pxmodrim._app import App
from pxmodrim.ui.context import AppContext
from pxmodrim.ui.ui_prefs import ResolvedScale, ScaleSource
from pxmodrim.ui.window import main_window as main_window_module
from pxmodrim.ui.window.main_window import MainWindow


def _app_stub(qapp: QApplication, arguments: list[str], source: ScaleSource) -> Any:
    return SimpleNamespace(
        qt_app=qapp,
        _original_arguments=arguments,
        _initial_scale=ResolvedScale(100, source),
    )


def _wait_for(path: Path) -> str:
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if path.exists() and path.read_text(encoding="utf-8"):
            return path.read_text(encoding="utf-8")
        time.sleep(0.05)
    raise AssertionError(f"{path} was not written")


@pytest.mark.parametrize(
    ("source", "expected"), [("saved", "unset"), ("qt-env", "1.5")]
)
def test_restart_relaunches_with_original_arguments_and_clean_scale_env(
    qapp: QApplication,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    source: ScaleSource,
    expected: str,
) -> None:
    marker = tmp_path / "marker"
    code = (
        "import os, sys; "
        f"open({str(marker)!r}, 'w').write(os.environ.get('QT_SCALE_FACTOR', 'unset')"
        " + ' ' + sys.argv[1])"
    )
    app = _app_stub(qapp, [sys.executable, "-c", code, "--flag"], source)
    # An app-generated factor lives in the parent's environment only.
    monkeypatch.setenv("QT_SCALE_FACTOR", "1.5")
    monkeypatch.delenv("APPIMAGE", raising=False)

    assert App._start_restart_process(app) is True

    assert _wait_for(marker) == f"{expected} --flag"


def test_restart_reports_launch_failure(
    qapp: QApplication, monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    monkeypatch.setattr(sys, "executable", str(tmp_path / "missing-python"))
    monkeypatch.delenv("APPIMAGE", raising=False)
    app = _app_stub(qapp, ["python"], "saved")

    assert App._start_restart_process(app) is False


_FROZEN_ARGUMENTS = ["/tmp/.mount_PxModR/PxModRim", "--ui-scale", "150"]


def _appimage_paths(tmp_path: Path) -> tuple[str, str, str]:
    appimage = tmp_path / "PxModRim-x86_64.AppImage"
    appimage.write_bytes(b"")
    appdir = tmp_path / "mount"
    appdir.mkdir()
    return str(appimage), str(appdir), str(appdir / "PxModRim")


def test_running_appimage_is_relaunched_by_its_outer_path(tmp_path: Path) -> None:
    appimage, appdir, executable = _appimage_paths(tmp_path)

    assert app_module._appimage_path(appimage, appdir, executable) == appimage


@pytest.mark.parametrize("keep_qt_scale_factor", [False, True])
def test_restart_command_keeps_arguments_and_scale_only_when_user_set(
    monkeypatch: pytest.MonkeyPatch, keep_qt_scale_factor: bool
) -> None:
    monkeypatch.setenv("QT_SCALE_FACTOR", "1.5")

    program, arguments, environment = app_module._restart_command(
        "/opt/PxModRim", _FROZEN_ARGUMENTS, keep_qt_scale_factor
    )

    assert program == "/opt/PxModRim"
    assert arguments == ["--ui-scale", "150"]
    assert environment.contains("QT_SCALE_FACTOR") is keep_qt_scale_factor


@pytest.mark.parametrize(
    ("appimage", "appdir", "executable"),
    [
        (None, "mount", "mount/PxModRim"),
        ("", "mount", "mount/PxModRim"),
        ("missing.AppImage", "mount", "mount/PxModRim"),
        ("PxModRim-x86_64.AppImage", None, "mount/PxModRim"),
        ("PxModRim-x86_64.AppImage", "", "mount/PxModRim"),
        ("PxModRim-x86_64.AppImage", "mount", "elsewhere/PxModRim"),
        ("PxModRim-x86_64.AppImage", "mount", "mount-other/PxModRim"),
    ],
)
def test_appimage_is_ignored_unless_this_process_runs_from_it(
    tmp_path: Path, appimage: str | None, appdir: str | None, executable: str
) -> None:
    _appimage_paths(tmp_path)

    def resolve(value: str | None) -> str | None:
        return str(tmp_path / value) if value else value

    assert (
        app_module._appimage_path(
            resolve(appimage), resolve(appdir), str(tmp_path / executable)
        )
        is None
    )


@pytest.mark.parametrize("restart_pending", [False, True])
def test_restart_waits_for_shutdown_and_closed_event_loop(
    monkeypatch: pytest.MonkeyPatch, restart_pending: bool
) -> None:
    events: list[str] = []
    loop = asyncio.new_event_loop()
    monkeypatch.setattr(app_module, "QEventLoop", lambda: loop)
    monkeypatch.setattr(
        main_window_module, "QApplication", SimpleNamespace(instance=lambda: None)
    )

    class Plugins:
        def __init__(self, name: str) -> None:
            self.name = name

        async def shutdown_all(self) -> None:
            events.append(f"{self.name} shutdown started")
            await asyncio.sleep(0)
            events.append(f"{self.name} shutdown finished")

    app_ctx = cast(
        AppContext,
        SimpleNamespace(
            _plugins=Plugins("ui"),
            _core=SimpleNamespace(plugins=Plugins("core")),
        ),
    )

    class Window(SimpleNamespace):
        def close(self) -> None:
            event = QCloseEvent()
            MainWindow.closeEvent(cast(MainWindow, self), event)
            assert event.isAccepted()

    window = Window(
        restart_pending=False,
        _unsaved_changes=False,
        _close_confirmed=False,
        _views=[],
        _update_task=None,
        _launch_task=None,
        _app_quit_callback=lambda: events.append("closed"),
        deleteLater=lambda: None,
    )

    class Application(SimpleNamespace):
        async def async_run(self) -> int:
            if restart_pending:
                MainWindow.request_restart(cast(MainWindow, window))
            else:
                window.close()
            assert events == ["closed"]
            await AppContext.shutdown_all(app_ctx)
            return 7

        def _start_restart_process(self) -> bool:
            assert loop.is_closed()
            events.append("spawned")
            return True

    app = Application(
        main_window=window,
        _ctx=SimpleNamespace(close_sync=lambda: events.append("closed sync")),
    )

    assert App.run(cast(App, app)) == 7
    assert events == [
        "closed",
        "ui shutdown started",
        "ui shutdown finished",
        "core shutdown started",
        "core shutdown finished",
        "closed sync",
        *(["spawned"] if restart_pending else []),
    ]
