from __future__ import annotations

import asyncio
import json
from pathlib import Path
from time import time

import aiosqlite

from pxmodrim.core.services.startup_impact_service.models import (
    StartupImpactMod,
    StartupImpactReport,
)

HISTORY_DEPTH = 5

# Recency weights: the most recent session gets WEIGHT_SPREAD, the next
# WEIGHT_SPREAD-1, ... down to 1. Sessions older than that are clamped to 1.
WEIGHT_SPREAD = 10

_SCHEMA = """
    CREATE TABLE IF NOT EXISTS startup_impact_sessions (
        id              INTEGER PRIMARY KEY AUTOINCREMENT,
        recorded_at     REAL NOT NULL,
        loading_time_s  REAL NOT NULL DEFAULT 0,
        timestamp       TEXT NOT NULL DEFAULT ''
    );

    CREATE TABLE IF NOT EXISTS startup_impact_entries (
        id                      INTEGER PRIMARY KEY AUTOINCREMENT,
        session_id              INTEGER NOT NULL,
        package_id              TEXT NOT NULL,
        mod_name                TEXT NOT NULL DEFAULT '',
        total_impact_s          REAL NOT NULL DEFAULT 0,
        metrics_json            TEXT NOT NULL DEFAULT '{}',
        off_thread_metrics_json TEXT NOT NULL DEFAULT '{}',
        off_thread_total_s      REAL NOT NULL DEFAULT 0,
        FOREIGN KEY (session_id)
            REFERENCES startup_impact_sessions(id) ON DELETE CASCADE
    );

    CREATE INDEX IF NOT EXISTS idx_sie_session
        ON startup_impact_entries(session_id);
    CREATE INDEX IF NOT EXISTS idx_sie_pid
        ON startup_impact_entries(package_id);
"""

_BASE_GAME_PID = "__base_game__"

# CTE ranking the most recent sessions by recency and assigning a descending
# weight (WEIGHT_SPREAD for the newest, WEIGHT_SPREAD-1 for the next, ... down
# to 1). Sessions older than WEIGHT_SPREAD clamp to weight 1.
#
# Bind parameters, in order: WEIGHT_SPREAD, HISTORY_DEPTH.
# Exposes columns: session_id, weight.
_WEIGHTED_SESSION_CTE = """
    ranked AS (
        SELECT id AS session_id,
               ROW_NUMBER() OVER (ORDER BY id DESC) AS rn
        FROM startup_impact_sessions
        ORDER BY id DESC
        LIMIT ?
    ),
    w AS (
        SELECT session_id,
               MAX(? - rn + 1, 1) AS weight
        FROM ranked
    )
"""

_CTE_PARAMS: tuple[int, int] = (HISTORY_DEPTH, WEIGHT_SPREAD)


def db_path(config_dir: Path) -> Path:
    return config_dir / "cache.db"


class StartupImpactDb:
    """Owns a single aiosqlite connection guarded by an asyncio lock."""

    __slots__ = ("_connection", "_connection_path", "_lock")

    def __init__(self) -> None:
        self._connection: aiosqlite.Connection | None = None
        self._connection_path: str = ""
        self._lock = asyncio.Lock()

    async def _connection_for(self, path: Path) -> aiosqlite.Connection:
        db_str = str(path)
        async with self._lock:
            if self._connection is None or self._connection_path != db_str:
                if self._connection is not None:
                    await self._connection.close()
                conn = await aiosqlite.connect(db_str, check_same_thread=False)
                await conn.execute("PRAGMA journal_mode=WAL")
                await conn.execute("PRAGMA busy_timeout=5000")
                await conn.execute("PRAGMA foreign_keys=ON")
                await conn.executescript(_SCHEMA)
                self._connection = conn
                self._connection_path = db_str
            return self._connection

    async def close(self) -> None:
        async with self._lock:
            if self._connection is not None:
                await self._connection.close()
                self._connection = None
                self._connection_path = ""

    def close_sync(self) -> None:
        if self._connection is not None:
            self._connection._conn.close()
            self._connection = None
            self._connection_path = ""

    async def store_report(self, path: Path, report: StartupImpactReport) -> None:
        if not path.exists():
            path.parent.mkdir(parents=True, exist_ok=True)
        db = await self._connection_for(path)
        async with db.cursor() as cursor:
            await cursor.execute(
                "INSERT INTO startup_impact_sessions"
                " (recorded_at, loading_time_s, timestamp) VALUES (?, ?, ?)",
                (time(), report.loading_time_s, report.timestamp),
            )
            sid = cursor.lastrowid
            rows = [
                (
                    sid,
                    mod.package_id or mod.mod_name,
                    mod.mod_name,
                    mod.total_impact_s,
                    json.dumps(mod.metrics),
                    json.dumps(mod.off_thread_metrics),
                    mod.off_thread_total_impact_s,
                )
                for mod in report.mods
            ]
            await cursor.executemany(
                "INSERT INTO startup_impact_entries"
                " (session_id, package_id, mod_name, total_impact_s,"
                "  metrics_json, off_thread_metrics_json, off_thread_total_s)"
                " VALUES (?, ?, ?, ?, ?, ?, ?)",
                rows,
            )
            await cursor.execute(
                "DELETE FROM startup_impact_sessions WHERE id NOT IN"
                " (SELECT id FROM startup_impact_sessions ORDER BY id DESC"
                " LIMIT ?)",
                (HISTORY_DEPTH,),
            )
        await db.commit()

    async def get_latest_report(self, path: Path) -> StartupImpactReport | None:
        if not path.exists():
            return None
        db = await self._connection_for(path)
        async with db.cursor() as cursor:
            await cursor.execute(
                "SELECT id, loading_time_s, timestamp"
                " FROM startup_impact_sessions ORDER BY id DESC LIMIT 1",
            )
            row = await cursor.fetchone()
            if not row:
                return None
            sid, loading_time_s, timestamp = row
            await cursor.execute(
                "SELECT package_id, mod_name, total_impact_s,"
                " metrics_json, off_thread_metrics_json, off_thread_total_s"
                " FROM startup_impact_entries WHERE session_id = ?",
                (sid,),
            )
            rows = await cursor.fetchall()
        mods = [_row_to_mod(r) for r in rows]
        return StartupImpactReport(
            path="",
            loading_time_s=loading_time_s,
            mods=tuple(mods),
            timestamp=timestamp,
        )

    async def get_average(self, path: Path, package_id: str) -> float:
        if not path.exists():
            return 0.0
        db = await self._connection_for(path)
        async with db.cursor() as cursor:
            await cursor.execute(
                "WITH " + _WEIGHTED_SESSION_CTE + """
                SELECT COALESCE(
                    SUM(e.total_impact_s * w.weight) * 1.0
                        / NULLIF(SUM(w.weight), 0),
                    0
                )
                FROM startup_impact_entries e
                JOIN w ON w.session_id = e.session_id
                WHERE e.package_id = ?
                """,
                (*_CTE_PARAMS, package_id),
            )
            row = await cursor.fetchone()
            return float(row[0]) if row else 0.0

    async def get_totals_for(
        self, path: Path, package_ids: list[str]
    ) -> dict[str, tuple[float, float]]:
        """Return {package_id: (on_thread_est, off_thread_est)} for the most
        recent sessions using recency-weighted averages, in a single query.

        Mods absent from the stored history default to (0.0, 0.0).
        """
        result: dict[str, tuple[float, float]] = dict.fromkeys(
            package_ids, (0.0, 0.0)
        )
        if not package_ids:
            return result
        if not path.exists():
            return result
        placeholders = ",".join("?" for _ in package_ids)
        db = await self._connection_for(path)
        async with db.cursor() as cursor:
            await cursor.execute(
                "WITH " + _WEIGHTED_SESSION_CTE + """
                SELECT e.package_id,
                       COALESCE(
                           SUM(e.total_impact_s * w.weight)
                               * 1.0 / NULLIF(SUM(w.weight), 0),
                           0
                       ),
                       COALESCE(
                           SUM(e.off_thread_total_s * w.weight)
                               * 1.0 / NULLIF(SUM(w.weight), 0),
                           0
                       )
                FROM startup_impact_entries e
                JOIN w ON w.session_id = e.session_id
                WHERE e.package_id IN (""" + placeholders + """)
                GROUP BY e.package_id
                """,
                (*_CTE_PARAMS, *package_ids),
            )
            for pid, on_thread, off_thread in await cursor.fetchall():
                result[str(pid)] = (float(on_thread), float(off_thread))
        return result

    async def get_all_averages(self, path: Path) -> dict[str, float]:
        if not path.exists():
            return {}
        db = await self._connection_for(path)
        async with db.cursor() as cursor:
            await cursor.execute(
                "WITH " + _WEIGHTED_SESSION_CTE + """
                SELECT e.package_id,
                       COALESCE(
                           SUM(e.total_impact_s * w.weight)
                               * 1.0 / NULLIF(SUM(w.weight), 0),
                           0
                       )
                FROM startup_impact_entries e
                JOIN w ON w.session_id = e.session_id
                GROUP BY e.package_id
                """,
                _CTE_PARAMS,
            )
            return {
                str(row[0]): float(row[1]) for row in await cursor.fetchall()
            }

    async def get_latest_with_averages(
        self,
        path: Path,
        active_pids: list[str],
        selected_pid: str | None = None,
    ) -> tuple[
        StartupImpactReport | None,
        float,
        dict[str, tuple[float, float]],
        float,
    ]:
        """Fetch the latest report plus base-game and per-mod impact averages in
        a single connection/transaction.

        Returns (latest_report, base_game_avg, pid_totals, selected_mod_avg)
        where pid_totals maps each active pid to (on_thread_avg, off_thread_avg)
        and selected_mod_avg is the on-thread average for the selected mod (0.0
        if none/absent).
        """
        if not path.exists():
            return None, 0.0, dict.fromkeys(active_pids, (0.0, 0.0)), 0.0

        db = await self._connection_for(path)
        async with db.cursor() as cursor:
            await cursor.execute(
                "SELECT id, loading_time_s, timestamp"
                " FROM startup_impact_sessions ORDER BY id DESC LIMIT 1",
            )
            row = await cursor.fetchone()
            report: StartupImpactReport | None = None
            if row is not None:
                sid, loading_time_s, timestamp = row
                await cursor.execute(
                    "SELECT package_id, mod_name, total_impact_s,"
                    " metrics_json, off_thread_metrics_json, off_thread_total_s"
                    " FROM startup_impact_entries WHERE session_id = ?",
                    (sid,),
                )
                mods = [_row_to_mod(r) for r in await cursor.fetchall()]
                report = StartupImpactReport(
                    path="",
                    loading_time_s=loading_time_s,
                    mods=tuple(mods),
                    timestamp=timestamp,
                )

            await cursor.execute(
                "WITH " + _WEIGHTED_SESSION_CTE + """
                SELECT COALESCE(
                    SUM(e.total_impact_s * w.weight) * 1.0
                        / NULLIF(SUM(w.weight), 0),
                    0
                )
                FROM startup_impact_entries e
                JOIN w ON w.session_id = e.session_id
                WHERE e.package_id = ?
                """,
                (*_CTE_PARAMS, _BASE_GAME_PID),
            )
            base_row = await cursor.fetchone()
            base_avg = float(base_row[0]) if base_row else 0.0

            totals: dict[str, tuple[float, float]] = dict.fromkeys(
                active_pids, (0.0, 0.0)
            )
            if active_pids:
                placeholders = ",".join("?" for _ in active_pids)
                await cursor.execute(
                    "WITH " + _WEIGHTED_SESSION_CTE + """
                    SELECT e.package_id,
                           COALESCE(
                               SUM(e.total_impact_s * w.weight)
                                   * 1.0 / NULLIF(SUM(w.weight), 0),
                               0
                           ),
                           COALESCE(
                               SUM(e.off_thread_total_s * w.weight)
                                   * 1.0 / NULLIF(SUM(w.weight), 0),
                               0
                           )
                    FROM startup_impact_entries e
                    JOIN w ON w.session_id = e.session_id
                    WHERE e.package_id IN (""" + placeholders + """)
                    GROUP BY e.package_id
                    """,
                    (*_CTE_PARAMS, *active_pids),
                )
                for pid, on_thread, off_thread in await cursor.fetchall():
                    totals[str(pid)] = (float(on_thread), float(off_thread))

            selected_avg = 0.0
            if selected_pid is not None:
                await cursor.execute(
                    "WITH " + _WEIGHTED_SESSION_CTE + """
                    SELECT COALESCE(
                        SUM(e.total_impact_s * w.weight) * 1.0
                            / NULLIF(SUM(w.weight), 0),
                        0
                    )
                    FROM startup_impact_entries e
                    JOIN w ON w.session_id = e.session_id
                    WHERE e.package_id = ?
                    """,
                    (*_CTE_PARAMS, selected_pid),
                )
                sel_row = await cursor.fetchone()
                selected_avg = float(sel_row[0]) if sel_row else 0.0

        return report, base_avg, totals, selected_avg

    async def clear(self, path: Path) -> None:
        if not path.exists():
            return
        db = await self._connection_for(path)
        async with db.cursor() as cursor:
            await cursor.execute("DELETE FROM startup_impact_entries")
            await cursor.execute("DELETE FROM startup_impact_sessions")
        await db.commit()


def _row_to_mod(row: aiosqlite.Row) -> StartupImpactMod:
    pid, name, impact, metrics_json, ot_json, ot_total = row
    pid_val: str | None = pid
    if pid == name:
        pid_val = None
    return StartupImpactMod(
        mod_name=name,
        package_id=pid_val,
        total_impact_s=impact,
        metrics=json.loads(metrics_json or "{}"),
        off_thread_metrics=json.loads(ot_json or "{}"),
        off_thread_total_impact_s=ot_total,
    )
