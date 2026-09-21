from __future__ import annotations

from pathlib import Path

import aiosqlite
import pytest

from pxmodrim.core.migrator import ensure_schema


async def _noop() -> None:
    return None


async def test_db_migrations_run_in_order_and_backup_before_steps(
    tmp_path: Path,
) -> None:
    path = tmp_path / "cache.db"
    async with aiosqlite.connect(path) as conn:
        await conn.execute("CREATE TABLE state (value TEXT)")
        await conn.execute("PRAGMA user_version = 1")
        await conn.commit()
        events: list[str] = []

        async def step_two() -> None:
            events.append("two")
            await conn.execute("ALTER TABLE state ADD COLUMN second TEXT")

        async def step_three() -> None:
            events.append("three")
            await conn.execute("ALTER TABLE state ADD COLUMN third TEXT")

        async def step_four() -> None:
            events.append("four")

        await ensure_schema(
            conn,
            schema_sql="CREATE TABLE IF NOT EXISTS state (value TEXT);",
            schema_version=3,
            steps={2: step_two, 3: step_three, 4: step_four},
            backup_path=path,
        )
        await conn.commit()

        row = await (await conn.execute("PRAGMA user_version")).fetchone()
        assert row == (3,)
        assert events == ["two", "three"]

    assert list(tmp_path.glob("cache.db.bak.*"))


async def test_db_missing_migration_step_fails_visibly(tmp_path: Path) -> None:
    path = tmp_path / "cache.db"
    async with aiosqlite.connect(path) as conn:
        await conn.execute("CREATE TABLE state (value TEXT)")
        await conn.execute("PRAGMA user_version = 1")
        await conn.commit()

        with pytest.raises(KeyError, match="Missing migration step"):
            await ensure_schema(
                conn,
                schema_sql="CREATE TABLE IF NOT EXISTS state (value TEXT);",
                schema_version=3,
                steps={2: _noop},
                backup_path=path,
            )
        row = await (await conn.execute("PRAGMA user_version")).fetchone()
        assert row == (1,)

    assert list(tmp_path.glob("cache.db.bak.*"))
