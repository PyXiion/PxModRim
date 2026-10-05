from __future__ import annotations

import asyncio
import os
import re
import shlex
import subprocess
import sys
import webbrowser
from pathlib import Path
from urllib.parse import quote

import psutil
from loguru import logger

from pxmodrim.core.constants import RIMWORLD_STEAM_APP_ID, LaunchStrategy
from pxmodrim.core.context import CoreContext

_PROCESS_NAMES = frozenset(
    {
        "RimWorldLinux",
        "RimWorldWin64.exe",
        "RimWorldWin.exe",
        "RimWorld.exe",
        "RimWorld by Ludeon Studios",
    }
)
_COMMAND_TOKEN = "%command%"
_ENV_ASSIGNMENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*=")
_POLL_START = 0.5
_POLL_EXIT = 2.0


def _split(text: str) -> list[str]:
    return shlex.split(text, posix=sys.platform != "win32")


def build_direct_command(
    exe: str, wrapper: str, args: str
) -> tuple[list[str], dict[str, str]]:
    """Expand wrapper/args into argv plus extra env.

    Wrapper follows Steam's convention: leading KEY=VALUE tokens are env vars and
    ``%command%`` marks where the game goes; without it the wrapper is a prefix.
    """
    game = [exe, *_split(args)]
    tokens = _split(wrapper)
    env: dict[str, str] = {}
    while tokens and _ENV_ASSIGNMENT.match(tokens[0]):
        key, _, value = tokens.pop(0).partition("=")
        env[key] = value
    if _COMMAND_TOKEN in tokens:
        i = tokens.index(_COMMAND_TOKEN)
        return [*tokens[:i], *game, *tokens[i + 1 :]], env
    return [*tokens, *game], env


class GameLauncher:
    __slots__ = ("_ctx", "_proc")

    def __init__(self, ctx: CoreContext) -> None:
        self._ctx = ctx
        self._proc: subprocess.Popen[bytes] | None = None

    async def launch(self, strategy: LaunchStrategy) -> tuple[bool, str]:
        if not self._ctx.config.paths.game:
            logger.warning("Launch aborted — game path not configured")
            return False, "Game path not configured"

        logger.info("Launching game with strategy: {}", strategy.name)
        if strategy == LaunchStrategy.DIRECT:
            return await self._launch_direct()
        return await self._launch_steam()

    @staticmethod
    def is_running() -> bool:
        for proc in psutil.process_iter(["name", "status"]):
            if (
                proc.info["name"] in _PROCESS_NAMES
                and proc.info["status"] != psutil.STATUS_ZOMBIE
            ):
                return True
        return False

    async def wait_for_start(self, timeout: float = 60.0) -> bool:
        """Wait until a game process appears; False if it died or never showed."""
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while loop.time() < deadline:
            if await asyncio.to_thread(self.is_running):
                return True
            if self._proc is not None and self._proc.poll() is not None:
                return False
            await asyncio.sleep(_POLL_START)
        return False

    async def wait_for_exit(self) -> int | None:
        """Block until no game process remains; the exit code if we spawned it."""
        while await asyncio.to_thread(self.is_running):
            if self._proc is not None:
                self._proc.poll()
            await asyncio.sleep(_POLL_EXIT)
        if self._proc is None:
            return None
        code = self._proc.poll()
        self._proc = None
        return code

    async def _launch_direct(self) -> tuple[bool, str]:
        game = Path(self._ctx.config.paths.game)
        exe = self._find_executable(game)
        if exe is None:
            logger.warning("Direct launch failed — executable not found in {}", game)
            return False, "Game executable not found"

        self._ensure_steam_appid(game)

        cfg = self._ctx.config
        try:
            if sys.platform == "darwin":
                argv = ["open", str(exe), "--args", *_split(cfg.launch_args)]
                env: dict[str, str] = {}
            else:
                argv, env = build_direct_command(
                    str(exe), cfg.launch_wrapper, cfg.launch_args
                )
        except ValueError as e:
            logger.warning("Invalid launch arguments: {}", e)
            return False, f"Invalid launch arguments: {e}"

        logger.info("Spawning process: {} (cwd: {})", argv, game)
        try:
            self._proc = await asyncio.to_thread(
                subprocess.Popen,
                argv,
                cwd=str(game),
                env={**os.environ, **env},
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
            return True, "Game launched"
        except (OSError, ValueError) as e:
            logger.warning("Direct launch failed: {}", e)
            return False, f"Failed to launch: {e}"

    async def _launch_steam(self) -> tuple[bool, str]:
        args = self._ctx.config.launch_args.strip()
        if args:
            url = f"steam://run/{RIMWORLD_STEAM_APP_ID}//{quote(args, safe='')}/"
        else:
            url = f"steam://rungameid/{RIMWORLD_STEAM_APP_ID}"
        logger.info("Opening Steam URL: {}", url)
        try:
            await asyncio.to_thread(webbrowser.open, url)
            return True, "Launching via Steam..."
        except webbrowser.Error as e:
            logger.warning("Steam launch failed: {}", e)
            return False, f"Steam launch failed: {e}"

    @staticmethod
    def _find_executable(game_path: Path) -> Path | None:
        if sys.platform == "linux":
            exe = game_path / "RimWorldLinux"
            return exe if exe.is_file() and os.access(exe, os.X_OK) else None
        if sys.platform == "darwin":
            apps = list(game_path.glob("*.app"))
            return apps[0] if apps else None
        if sys.platform == "win32":
            for name in ("RimWorldWin64.exe", "RimWorldWin.exe", "RimWorld.exe"):
                exe = game_path / name
                if exe.is_file():
                    return exe
            return None
        return None

    @staticmethod
    def _ensure_steam_appid(game_path: Path) -> None:
        app_id_path = (
            game_path.parent / "steam_appid.txt"
            if sys.platform == "darwin"
            else game_path / "steam_appid.txt"
        )
        if not app_id_path.exists():
            try:
                app_id_path.write_text(RIMWORLD_STEAM_APP_ID)
                logger.debug("Created {}", app_id_path)
            except OSError as e:
                logger.warning("Failed to create steam_appid.txt: {}", e)
