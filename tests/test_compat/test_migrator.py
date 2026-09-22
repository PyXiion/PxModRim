from __future__ import annotations

from pathlib import Path

import aiosqlite
import pytest

import pxmodrim.core.migrator as migrator_module
from pxmodrim.core.migrator import ensure_schema


async def _noop() -> None:
    return None


async def test_db_fresh_init_creates_schema_and_stamps_target(
    tmp_path: Path,
) -> None:
    path = tmp_path / "cache.db"
    events: list[str] = []

    async def future_step() -> None:
        events.append("future")

    async with aiosqlite.connect(path) as conn:
        await ensure_schema(
            conn,
            schema_sql="CREATE TABLE state (value TEXT);",
            schema_version=2,
            steps={1: _noop, 2: future_step},
            backup_path=path,
        )
        await conn.commit()

        version = await (await conn.execute("PRAGMA user_version")).fetchone()
        table = await (
            await conn.execute(
                "SELECT name FROM sqlite_master WHERE type = 'table' AND name = 'state'"
            )
        ).fetchone()

        assert version == (2,)
        assert table == ("state",)
        assert events == []

    assert not list(tmp_path.glob("cache.db.bak.*"))


async def test_db_migrations_checkpoint_wal_before_backup(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    path = tmp_path / "cache.db"
    monkeypatch.setattr(migrator_module, "time", lambda: 1_700_000_000)
    backup_path = tmp_path / "cache.db.bak.1700000000"

    async with aiosqlite.connect(path) as conn:
        journal_mode = await (await conn.execute("PRAGMA journal_mode = WAL")).fetchone()
        assert journal_mode == ("wal",)
        await conn.execute("PRAGMA wal_autocheckpoint = 0")
        await conn.execute("CREATE TABLE state (value TEXT)")
        await conn.execute("INSERT INTO state VALUES ('before')")
        await conn.execute("PRAGMA user_version = 1")
        await conn.commit()
        assert path.with_name(f"{path.name}-wal").stat().st_size > 0
        events: list[str] = []

        async def step_two() -> None:
            events.append("two")
            await conn.execute("ALTER TABLE state ADD COLUMN second TEXT")

        async def step_three() -> None:
            events.append("three")
            await conn.execute("ALTER TABLE state ADD COLUMN third TEXT")

        await ensure_schema(
            conn,
            schema_sql="CREATE TABLE IF NOT EXISTS state (value TEXT);",
            schema_version=3,
            steps={2: step_two, 3: step_three},
            backup_path=path,
        )
        await conn.commit()

        version = await (await conn.execute("PRAGMA user_version")).fetchone()
        columns = await (await conn.execute("PRAGMA table_info(state)")).fetchall()

        assert version == (3,)
        assert [column[1] for column in columns] == ["value", "second", "third"]
        assert events == ["two", "three"]

    for suffix in ("-wal", "-shm"):
        backup_path.with_name(f"{backup_path.name}{suffix}").unlink(missing_ok=True)

    async with aiosqlite.connect(backup_path) as backup_conn:
        backup_version = await (
            await backup_conn.execute("PRAGMA user_version")
        ).fetchone()
        backup_columns = await (
            await backup_conn.execute("PRAGMA table_info(state)")
        ).fetchall()
        backup_rows = await (
            await backup_conn.execute("SELECT value FROM state")
        ).fetchall()

    assert backup_version == (1,)
    assert [column[1] for column in backup_columns] == ["value"]
    assert backup_rows == [("before",)]


async def test_db_missing_migration_step_fails_without_advancing_version(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    path = tmp_path / "cache.db"
    monkeypatch.setattr(migrator_module, "time", lambda: 1_700_000_000)
    backup_path = tmp_path / "cache.db.bak.1700000000"

    async with aiosqlite.connect(path) as conn:
        await conn.execute("CREATE TABLE state (value TEXT)")
        await conn.execute("PRAGMA user_version = 1")
        await conn.commit()
        before = path.read_bytes()

        with pytest.raises(KeyError, match="Missing migration step"):
            await ensure_schema(
                conn,
                schema_sql="CREATE TABLE IF NOT EXISTS state (value TEXT);",
                schema_version=3,
                steps={2: _noop},
                backup_path=path,
            )
        version = await (await conn.execute("PRAGMA user_version")).fetchone()

        assert version == (1,)

    assert path.read_bytes() == before
    assert backup_path.read_bytes() == before


async def test_db_current_schema_is_a_noop(tmp_path: Path) -> None:
    path = tmp_path / "cache.db"
    async with aiosqlite.connect(path) as conn:
        await conn.execute("CREATE TABLE state (value TEXT)")
        await conn.execute("PRAGMA user_version = 1")
        await conn.commit()
        before = path.read_bytes()
        events: list[str] = []

        async def unexpected_step() -> None:
            events.append("called")

        await ensure_schema(
            conn,
            schema_sql="CREATE TABLE should_not_be_created (value TEXT);",
            schema_version=1,
            steps={1: unexpected_step},
            backup_path=path,
        )

        assert await (await conn.execute("PRAGMA user_version")).fetchone() == (1,)
        assert events == []

    assert path.read_bytes() == before
    assert not list(tmp_path.glob("cache.db.bak.*"))


async def test_db_newer_schema_is_a_noop(tmp_path: Path) -> None:
    path = tmp_path / "cache.db"
    async with aiosqlite.connect(path) as conn:
        await conn.execute("CREATE TABLE state (value TEXT)")
        await conn.execute("PRAGMA user_version = 2")
        await conn.commit()
        before = path.read_bytes()
        events: list[str] = []

        async def unexpected_step() -> None:
            events.append("called")

        await ensure_schema(
            conn,
            schema_sql="CREATE TABLE should_not_be_created (value TEXT);",
            schema_version=1,
            steps={1: unexpected_step},
            backup_path=path,
        )

        assert await (await conn.execute("PRAGMA user_version")).fetchone() == (2,)
        assert events == []

    assert path.read_bytes() == before
    assert not list(tmp_path.glob("cache.db.bak.*"))
