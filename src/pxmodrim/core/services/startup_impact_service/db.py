from __future__ import annotations

from collections.abc import Sequence
from pathlib import Path
from time import time

import aiosqlite

from pxmodrim.core.services.startup_impact_service.models import (
    StartupImpactMod,
    StartupImpactReport,
)

HISTORY_DEPTH = 5


def db_path(config_dir: Path) -> Path:
    return config_dir / "cache.db"


async def ensure_schema(path: Path) -> None:
    async with aiosqlite.connect(str(path)) as db:
        await db.execute("PRAGMA journal_mode=WAL")
        await db.executescript("""
            CREATE TABLE IF NOT EXISTS startup_impact_sessions (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                recorded_at REAL NOT NULL
            );
            CREATE TABLE IF NOT EXISTS startup_impact_entries (
                package_id     TEXT NOT NULL,
                total_impact_s REAL NOT NULL DEFAULT 0,
                session_id     INTEGER NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_sie_pid
                ON startup_impact_entries(package_id);
        """)


async def store(path: Path, entries: Sequence[tuple[str, float]]) -> None:
    await ensure_schema(path)
    async with aiosqlite.connect(str(path)) as db:
        cursor = await db.execute(
            "INSERT INTO startup_impact_sessions (recorded_at) VALUES (?)",
            (time(),),
        )
        sid = cursor.lastrowid
        await db.executemany(
            "INSERT INTO startup_impact_entries"
            " (package_id, total_impact_s, session_id) VALUES (?, ?, ?)",
            [(pid, impact, sid) for pid, impact in entries],
        )
        await db.execute(
            "DELETE FROM startup_impact_sessions WHERE id NOT IN"
            " (SELECT id FROM startup_impact_sessions ORDER BY id DESC LIMIT ?)",
            (HISTORY_DEPTH,),
        )
        await db.commit()


async def load_cached(path: Path) -> StartupImpactReport | None:
    if not path.exists():
        return None
    try:
        await ensure_schema(path)
        async with aiosqlite.connect(str(path)) as db:
            cursor = await db.execute(
                """
                SELECT e.package_id, AVG(e.total_impact_s)
                FROM startup_impact_entries e
                JOIN startup_impact_sessions s ON s.id = e.session_id
                WHERE s.id > (
                    SELECT COALESCE(MAX(id) - ?, 0) FROM startup_impact_sessions
                )
                GROUP BY e.package_id
                """,
                (HISTORY_DEPTH,),
            )
            rows = await cursor.fetchall()
            if not rows:
                return None
            mods = tuple(
                StartupImpactMod(
                    mod_name="",
                    package_id=str(r[0]),
                    total_impact_s=float(r[1]),
                )
                for r in rows
            )
            return StartupImpactReport(
                path="(cache)",
                loading_time_s=0.0,
                mods=mods,
            )
    except aiosqlite.DatabaseError:
        return None


async def clear(path: Path) -> None:
    if not path.exists():
        return
    async with aiosqlite.connect(str(path)) as db:
        await db.execute("DELETE FROM startup_impact_entries")
        await db.execute("DELETE FROM startup_impact_sessions")
        await db.commit()
