from __future__ import annotations

import sys

import pytest

from pxmodrim.core.services.game_launcher import build_direct_command


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


def test_unbalanced_quotes_raise() -> None:
    with pytest.raises(ValueError):
        build_direct_command("/g/rw", "", '"oops')
