from __future__ import annotations

import shutil
from collections.abc import Awaitable, Callable
from pathlib import Path
from time import time
from typing import Any

from loguru import logger

MigrationFn = Callable[[], Awaitable[None]]
OnStepSuccess = Callable[[int], Awaitable[None]]


async def ensure_schema(
    conn: Any,
    *,
    schema_sql: str,
    schema_version: int,
    steps: dict[int, MigrationFn],
    backup_path: Path | None = None,
    label: str = "schema",
) -> None:
    """Stamp, init, or migrate a ``PRAGMA user_version`` schema to ``schema_version``.

    ``steps`` maps target version to a migration coroutine; may be empty when no
    migrations exist yet. A file copy backup of ``backup_path`` is made before any
    migration that is not a fresh init.
    """
    row = await conn.execute("PRAGMA user_version")
    fetched = await row.fetchone()
    current = fetched[0] if fetched is not None else 0

    if current >= schema_version:
        return

    async def stamp(v: int) -> None:
        await conn.execute(f"PRAGMA user_version = {v}")

    if current == 0:
        logger.info("Initializing {} (version {})", label, schema_version)
        await conn.executescript(schema_sql)
        await stamp(schema_version)
        return

    logger.info("Migrating {} {} -> {}", label, current, schema_version)
    if backup_path is not None:
        backup = backup_path.with_suffix(f".db.bak.{int(time())}")
        shutil.copy2(backup_path, backup)

    if not steps:
        await stamp(schema_version)
        return

    migrator = Migrator(steps)
    await migrator.migrate(current, on_step=stamp)
    await stamp(schema_version)


class Migrator:
    __slots__ = ("_latest", "_steps")

    def __init__(self, steps: dict[int, MigrationFn]) -> None:
        if not steps:
            raise ValueError("steps must not be empty")
        self._latest = max(steps.keys())
        self._steps = steps

    async def migrate(self, current: int, on_step: OnStepSuccess | None = None) -> bool:
        if current >= self._latest:
            return False

        for target in range(current + 1, self._latest + 1):
            fn = self._steps.get(target)
            if fn is None:
                raise KeyError(f"Missing migration step for target version {target}")
            await fn()
            if on_step is not None:
                await on_step(target)

        return True
