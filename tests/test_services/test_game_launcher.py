from __future__ import annotations

import sys
import types

import pytest

from pxmodrim.core.config import AppConfig, PathConfig
from pxmodrim.core.constants import RIMWORLD_STEAM_APP_ID, LaunchStrategy
from pxmodrim.core.services.game_launcher import GameLauncher, build_direct_command


def test_no_wrapper_appends_args() -> None:
    argv, env = build_direct_command("/g/RimWorldLinux", "", "-popupwindow -logfile")
    assert argv == ["/g/RimWorldLinux", "-popupwindow", "-logfile"]
    assert env == {}


def test_wrapper_without_placeholder_is_prefix() -> None:
    argv, _ = build_direct_command("/g/rw", "gamemoderun", "-x")
    assert argv == ["gamemoderun", "/g/rw", "-x"]


def test_placeholder_position_and_env_split() -> None:
    argv, env = build_direct_command(
        "/g/rw", "MANGOHUD=1 DXVK_HUD=fps mangohud %command% --tail", "-x"
    )
    assert argv == ["mangohud", "/g/rw", "-x", "--tail"]
    assert env == {"MANGOHUD": "1", "DXVK_HUD": "fps"}


@pytest.mark.skipif(sys.platform == "win32", reason="posix quoting")
def test_quoted_args_stay_single_token() -> None:
    argv, _ = build_direct_command("/g/rw", "", '-savedatafolder="/tmp/my saves"')
    assert argv == ["/g/rw", "-savedatafolder=/tmp/my saves"]


def test_option_with_equals_is_not_an_env_var() -> None:
    argv, env = build_direct_command("/g/rw", "--cfg=/p wrap", "")
    assert argv == ["--cfg=/p", "wrap", "/g/rw"]
    assert env == {}


def test_unbalanced_quotes_raise() -> None:
    with pytest.raises(ValueError):
        build_direct_command("/g/rw", "", '"oops')


class _Proc:
    def __init__(self, name: str, status: str = "running") -> None:
        self.info = {"name": name, "status": status}


def _patch_procs(monkeypatch: pytest.MonkeyPatch, procs: list[list[_Proc]]) -> None:
    snapshots = iter(procs)
    last: list[_Proc] = []

    def fake_iter(_attrs: object = None) -> list[_Proc]:
        nonlocal last
        last = next(snapshots, last)
        return last

    monkeypatch.setattr(
        "pxmodrim.core.services.game_launcher.psutil.process_iter", fake_iter
    )


def test_zombie_game_process_is_not_running(monkeypatch: pytest.MonkeyPatch) -> None:
    _patch_procs(monkeypatch, [[_Proc("RimWorldLinux", "zombie"), _Proc("bash")]])
    assert GameLauncher.is_running() is False


@pytest.mark.asyncio
async def test_wait_for_exit_returns_once_process_disappears(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr("pxmodrim.core.services.game_launcher._POLL_EXIT", 0)
    _patch_procs(monkeypatch, [[_Proc("RimWorldLinux")], [_Proc("RimWorldLinux")], []])
    launcher = GameLauncher(types.SimpleNamespace(config=AppConfig()))  # type: ignore[arg-type]
    assert await launcher.wait_for_exit() is None


@pytest.mark.asyncio
async def test_steam_launch_passes_encoded_args(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    opened: list[str] = []
    monkeypatch.setattr(
        "pxmodrim.core.services.game_launcher.webbrowser.open", opened.append
    )
    cfg = AppConfig(paths=PathConfig(game="/g"), launch_args="-savedatafolder=/a b")
    launcher = GameLauncher(types.SimpleNamespace(config=cfg))  # type: ignore[arg-type]
    ok, _ = await launcher.launch(LaunchStrategy.STEAM)
    assert ok
    assert opened == [
        f"steam://run/{RIMWORLD_STEAM_APP_ID}//-savedatafolder%3D%2Fa%20b/"
    ]


class _Popen:
    def __init__(self, code: int | None) -> None:
        self.code = code

    def poll(self) -> int | None:
        return self.code


@pytest.mark.asyncio
async def test_wait_for_exit_returns_spawned_exit_code(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr("pxmodrim.core.services.game_launcher._POLL_EXIT", 0)
    _patch_procs(monkeypatch, [[_Proc("RimWorldLinux")], []])
    launcher = GameLauncher(types.SimpleNamespace(config=AppConfig()))  # type: ignore[arg-type]
    launcher._proc = _Popen(7)  # type: ignore[assignment]
    assert await launcher.wait_for_exit() == 7


@pytest.mark.asyncio
async def test_wait_for_start_true_once_process_appears(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr("pxmodrim.core.services.game_launcher._POLL_START", 0)
    _patch_procs(monkeypatch, [[], [], [_Proc("RimWorldLinux")]])
    launcher = GameLauncher(types.SimpleNamespace(config=AppConfig()))  # type: ignore[arg-type]
    assert await launcher.wait_for_start(5) is True


@pytest.mark.asyncio
async def test_wait_for_start_false_when_spawned_process_died(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr("pxmodrim.core.services.game_launcher._POLL_START", 0)
    _patch_procs(monkeypatch, [[]])
    launcher = GameLauncher(types.SimpleNamespace(config=AppConfig()))  # type: ignore[arg-type]
    launcher._proc = _Popen(1)  # type: ignore[assignment]
    assert await launcher.wait_for_start(5) is False
