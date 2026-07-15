from __future__ import annotations

import asyncio
import os
import subprocess
import sys
import webbrowser
from pathlib import Path

from loguru import logger

from pxmodrim.core.config import LaunchStrategy
from pxmodrim.core.constants import RIMWORLD_STEAM_APP_ID
from pxmodrim.core.context import CoreContext


class GameLauncher:
    __slots__ = ("_ctx",)

    def __init__(self, ctx: CoreContext) -> None:
        self._ctx = ctx

    async def launch(self) -> tuple[bool, str]:
        if not self._ctx.config.paths.game:
            logger.warning("Launch aborted — game path not configured")
            return False, "Game path not configured"

        strategy = self._ctx.config.ui.launch_strategy
        logger.info("Launching game with strategy: {}", strategy.name)
        if strategy == LaunchStrategy.DIRECT:
            return await self._launch_direct()
        return await self._launch_steam()

    async def _launch_direct(self) -> tuple[bool, str]:
        game = Path(self._ctx.config.paths.game)
        exe = self._find_executable(game)
        if exe is None:
            logger.warning("Direct launch failed — executable not found in {}", game)
            return False, "Game executable not found"

        self._ensure_steam_appid(game)

        logger.info("Spawning process: {} (cwd: {})", exe, game)
        try:
            if sys.platform == "darwin":
                popen_args = ["open", str(exe), "--args"]
                proc_cwd = str(game)
            else:
                popen_args = [str(exe)]
                proc_cwd = str(game)

            await asyncio.to_thread(
                subprocess.Popen,
                popen_args,
                cwd=proc_cwd,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
            return True, "Game launched"
        except Exception as e:
            logger.warning("Direct launch failed: {}", e)
            return False, f"Failed to launch: {e}"

    async def _launch_steam(self) -> tuple[bool, str]:
        url = f"steam://rungameid/{RIMWORLD_STEAM_APP_ID}"
        logger.info("Opening Steam URL: {}", url)
        try:
            await asyncio.to_thread(webbrowser.open, url)
            return True, "Launching via Steam..."
        except Exception as e:
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
            except Exception as e:
                logger.warning("Failed to create steam_appid.txt: {}", e)
