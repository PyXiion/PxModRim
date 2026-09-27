from __future__ import annotations

import asyncio
import sqlite3
from collections.abc import Iterable, Sequence
from pathlib import Path

import aiosqlite

from pxmodrim.core.migrator import ensure_schema
from pxmodrim.core.organizer.defaults import StandardRule
from pxmodrim.core.organizer.models import (
    ROOT_ID,
    Folder,
    OrganizerError,
    OrganizerState,
    Rule,
    RuleSpec,
    Tag,
)

_SCHEMA = """
CREATE TABLE IF NOT EXISTS folders (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    parent_id INTEGER REFERENCES folders(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    collapsed INTEGER NOT NULL DEFAULT 0,
    CHECK ((id = 1) = (parent_id IS NULL))
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_folders_parent_name
    ON folders (parent_id, name COLLATE NOCASE);

CREATE TABLE IF NOT EXISTS placements (
    package_id TEXT PRIMARY KEY COLLATE NOCASE,
    folder_id INTEGER NOT NULL REFERENCES folders(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS tags (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE COLLATE NOCASE,
    color TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS mod_tags (
    package_id TEXT NOT NULL COLLATE NOCASE,
    tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
    PRIMARY KEY (package_id, tag_id)
);

CREATE TABLE IF NOT EXISTS rules (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    position INTEGER NOT NULL,
    field TEXT NOT NULL CHECK (field IN ('package_id', 'name', 'author')),
    op TEXT NOT NULL CHECK (op IN ('prefix', 'contains', 'equals')),
    pattern TEXT NOT NULL,
    folder_id INTEGER NOT NULL REFERENCES folders(id) ON DELETE CASCADE
);

INSERT OR IGNORE INTO folders (id, parent_id, name, collapsed)
VALUES (1, NULL, 'Root', 0);
"""

_SCHEMA_VERSION = 1


def db_path(config_dir: Path) -> Path:
    return config_dir / "organizer.db"


class OrganizerDb:
    """Owns an aiosqlite connection guarded by an asyncio lock."""

    __slots__ = ("_connection", "_lock", "_path")

    def __init__(self, path: Path) -> None:
        self._path = path
        self._connection: aiosqlite.Connection | None = None
        self._lock = asyncio.Lock()

    async def _ensure_schema(self, conn: aiosqlite.Connection) -> None:
        await ensure_schema(
            conn,
            schema_sql=_SCHEMA,
            schema_version=_SCHEMA_VERSION,
            steps={},
            backup_path=self._path,
            label="organizer DB",
        )

    async def _get_conn(self) -> aiosqlite.Connection:
        if self._connection is None:
            if not self._path.parent.exists():
                self._path.parent.mkdir(parents=True, exist_ok=True)
            conn = await aiosqlite.connect(str(self._path), check_same_thread=False)
            await conn.execute("PRAGMA journal_mode=WAL")
            await conn.execute("PRAGMA busy_timeout=5000")
            await conn.execute("PRAGMA foreign_keys=ON")
            await self._ensure_schema(conn)
            self._connection = conn
        return self._connection

    async def close(self) -> None:
        async with self._lock:
            if self._connection is not None:
                await self._connection.close()
                self._connection = None

    def close_sync(self) -> None:
        if self._connection is not None:
            self._connection._conn.close()
            self._connection = None

    async def load(self) -> OrganizerState:
        async with self._lock:
            conn = await self._get_conn()

            folders: dict[int, Folder] = {}
            async with conn.execute(
                "SELECT id, parent_id, name, collapsed FROM folders"
            ) as cur:
                async for row in cur:
                    folders[row[0]] = Folder(
                        id=row[0],
                        parent_id=row[1],
                        name=row[2],
                        collapsed=bool(row[3]),
                    )

            placements: dict[str, int] = {}
            async with conn.execute(
                "SELECT package_id, folder_id FROM placements"
            ) as cur:
                async for row in cur:
                    placements[row[0].lower()] = row[1]

            tags: dict[int, Tag] = {}
            async with conn.execute("SELECT id, name, color FROM tags") as cur:
                async for row in cur:
                    tags[row[0]] = Tag(
                        id=row[0],
                        name=row[1],
                        color=row[2],
                    )

            mod_tags_builder: dict[str, set[int]] = {}
            async with conn.execute("SELECT package_id, tag_id FROM mod_tags") as cur:
                async for row in cur:
                    pid = row[0].lower()
                    mod_tags_builder.setdefault(pid, set()).add(row[1])
            mod_tags = {k: frozenset(v) for k, v in mod_tags_builder.items()}

            rules_list: list[Rule] = []
            async with conn.execute(
                """
                SELECT id, position, field, op, pattern, folder_id
                FROM rules
                ORDER BY position ASC, id ASC
                """
            ) as cur:
                async for row in cur:
                    rules_list.append(
                        Rule(
                            id=row[0],
                            position=row[1],
                            field=row[2],
                            op=row[3],
                            pattern=row[4],
                            folder_id=row[5],
                        )
                    )

            return OrganizerState(
                folders=folders,
                placements=placements,
                tags=tags,
                mod_tags=mod_tags,
                rules=tuple(rules_list),
            )

    async def create_folder(self, parent_id: int, name: str) -> Folder:
        async with self._lock:
            conn = await self._get_conn()
            try:
                cur = await conn.execute(
                    "INSERT INTO folders (parent_id, name) VALUES (?, ?)",
                    (parent_id, name),
                )
                folder_id = cur.lastrowid
                assert folder_id is not None
                await conn.commit()
                return Folder(
                    id=folder_id,
                    parent_id=parent_id,
                    name=name,
                    collapsed=False,
                )
            except (sqlite3.IntegrityError, aiosqlite.IntegrityError) as err:
                await conn.rollback()
                raise OrganizerError(str(err)) from err
            except Exception:
                await conn.rollback()
                raise

    async def create_folder_with(
        self, parent_id: int, name: str, package_ids: Iterable[str]
    ) -> Folder:
        pids = list(dict.fromkeys(p.strip().lower() for p in package_ids if p.strip()))
        async with self._lock:
            conn = await self._get_conn()
            try:
                cur = await conn.execute(
                    "INSERT INTO folders (parent_id, name) VALUES (?, ?)",
                    (parent_id, name),
                )
                folder_id = cur.lastrowid
                assert folder_id is not None
                if pids:
                    await conn.executemany(
                        """
                        INSERT INTO placements (package_id, folder_id)
                        VALUES (?, ?)
                        ON CONFLICT (package_id)
                        DO UPDATE SET folder_id = excluded.folder_id
                        """,
                        [(pid, folder_id) for pid in pids],
                    )
                await conn.commit()
                return Folder(
                    id=folder_id,
                    parent_id=parent_id,
                    name=name,
                    collapsed=False,
                )
            except (sqlite3.IntegrityError, aiosqlite.IntegrityError) as err:
                await conn.rollback()
                raise OrganizerError(str(err)) from err
            except Exception:
                await conn.rollback()
                raise

    async def rename_folder(self, folder_id: int, name: str) -> None:
        if folder_id == ROOT_ID:
            raise OrganizerError("The root folder cannot be renamed.")
        async with self._lock:
            conn = await self._get_conn()
            try:
                cur = await conn.execute(
                    "UPDATE folders SET name = ? WHERE id = ?", (name, folder_id)
                )
                if cur.rowcount == 0:
                    raise OrganizerError(f"Folder with id {folder_id} does not exist.")
                await conn.commit()
            except (sqlite3.IntegrityError, aiosqlite.IntegrityError) as err:
                await conn.rollback()
                raise OrganizerError(str(err)) from err
            except Exception:
                await conn.rollback()
                raise

    async def set_parent(self, folder_id: int, parent_id: int) -> None:
        if folder_id == ROOT_ID:
            raise OrganizerError("The root folder cannot be moved.")
        if folder_id == parent_id:
            raise OrganizerError("A folder cannot be its own parent.")
        async with self._lock:
            conn = await self._get_conn()
            try:
                cur = await conn.execute(
                    "UPDATE folders SET parent_id = ? WHERE id = ?",
                    (parent_id, folder_id),
                )
                if cur.rowcount == 0:
                    raise OrganizerError(f"Folder with id {folder_id} does not exist.")
                await conn.commit()
            except (sqlite3.IntegrityError, aiosqlite.IntegrityError) as err:
                await conn.rollback()
                raise OrganizerError(str(err)) from err
            except Exception:
                await conn.rollback()
                raise

    async def set_collapsed(self, folder_id: int, collapsed: bool) -> None:
        async with self._lock:
            conn = await self._get_conn()
            try:
                cur = await conn.execute(
                    "UPDATE folders SET collapsed = ? WHERE id = ?",
                    (1 if collapsed else 0, folder_id),
                )
                if cur.rowcount == 0:
                    raise OrganizerError(f"Folder with id {folder_id} does not exist.")
                await conn.commit()
            except (sqlite3.IntegrityError, aiosqlite.IntegrityError) as err:
                await conn.rollback()
                raise OrganizerError(str(err)) from err
            except Exception:
                await conn.rollback()
                raise

    async def delete_folder(self, folder_id: int) -> None:
        if folder_id == ROOT_ID:
            raise OrganizerError("The root folder cannot be deleted.")
        async with self._lock:
            conn = await self._get_conn()
            try:
                async with conn.execute(
                    "SELECT 1 FROM folders WHERE id = ?", (folder_id,)
                ) as cur:
                    if await cur.fetchone() is None:
                        raise OrganizerError(
                            f"Folder with id {folder_id} does not exist."
                        )

                # Collect subtree with recursive CTE and move placements to root
                await conn.execute(
                    """
                    WITH RECURSIVE subtree(id) AS (
                        SELECT ?
                        UNION ALL
                        SELECT f.id FROM folders f JOIN subtree s ON f.parent_id = s.id
                    )
                    UPDATE placements SET folder_id = ?
                    WHERE folder_id IN (SELECT id FROM subtree)
                    """,
                    (folder_id, ROOT_ID),
                )
                await conn.execute("DELETE FROM folders WHERE id = ?", (folder_id,))
                await conn.commit()
            except (sqlite3.IntegrityError, aiosqlite.IntegrityError) as err:
                await conn.rollback()
                raise OrganizerError(str(err)) from err
            except Exception:
                await conn.rollback()
                raise

    async def place(self, package_ids: Iterable[str], folder_id: int) -> None:
        pids = [p.strip().lower() for p in package_ids if p.strip()]
        if not pids:
            return
        async with self._lock:
            conn = await self._get_conn()
            try:
                await conn.executemany(
                    """
                    INSERT INTO placements (package_id, folder_id)
                    VALUES (?, ?)
                    ON CONFLICT (package_id)
                    DO UPDATE SET folder_id = excluded.folder_id
                    """,
                    [(pid, folder_id) for pid in pids],
                )
                await conn.commit()
            except (sqlite3.IntegrityError, aiosqlite.IntegrityError) as err:
                await conn.rollback()
                raise OrganizerError(str(err)) from err
            except Exception:
                await conn.rollback()
                raise

    async def unplace(self, package_ids: Iterable[str]) -> None:
        pids = [p.strip().lower() for p in package_ids if p.strip()]
        if not pids:
            return
        async with self._lock:
            conn = await self._get_conn()
            try:
                await conn.executemany(
                    "DELETE FROM placements WHERE package_id = ?",
                    [(pid,) for pid in pids],
                )
                await conn.commit()
            except (sqlite3.IntegrityError, aiosqlite.IntegrityError) as err:
                await conn.rollback()
                raise OrganizerError(str(err)) from err
            except Exception:
                await conn.rollback()
                raise

    async def create_tag(self, name: str, color: str) -> Tag:
        async with self._lock:
            conn = await self._get_conn()
            try:
                cur = await conn.execute(
                    "INSERT INTO tags (name, color) VALUES (?, ?)", (name, color)
                )
                tag_id = cur.lastrowid
                assert tag_id is not None
                await conn.commit()
                return Tag(id=tag_id, name=name, color=color)
            except (sqlite3.IntegrityError, aiosqlite.IntegrityError) as err:
                await conn.rollback()
                raise OrganizerError(str(err)) from err
            except Exception:
                await conn.rollback()
                raise

    async def update_tag(self, tag_id: int, name: str, color: str) -> None:
        async with self._lock:
            conn = await self._get_conn()
            try:
                cur = await conn.execute(
                    "UPDATE tags SET name = ?, color = ? WHERE id = ?",
                    (name, color, tag_id),
                )
                if cur.rowcount == 0:
                    raise OrganizerError(f"Tag with id {tag_id} does not exist.")
                await conn.commit()
            except (sqlite3.IntegrityError, aiosqlite.IntegrityError) as err:
                await conn.rollback()
                raise OrganizerError(str(err)) from err
            except Exception:
                await conn.rollback()
                raise

    async def delete_tag(self, tag_id: int) -> None:
        async with self._lock:
            conn = await self._get_conn()
            try:
                cur = await conn.execute("DELETE FROM tags WHERE id = ?", (tag_id,))
                if cur.rowcount == 0:
                    raise OrganizerError(f"Tag with id {tag_id} does not exist.")
                await conn.commit()
            except (sqlite3.IntegrityError, aiosqlite.IntegrityError) as err:
                await conn.rollback()
                raise OrganizerError(str(err)) from err
            except Exception:
                await conn.rollback()
                raise

    async def set_mod_tags(
        self,
        package_ids: Iterable[str],
        add: Iterable[int] = (),
        remove: Iterable[int] = (),
    ) -> None:
        pids = [p.strip().lower() for p in package_ids if p.strip()]
        add_ids = list(add)
        remove_ids = list(remove)
        if not pids or (not add_ids and not remove_ids):
            return

        async with self._lock:
            conn = await self._get_conn()
            try:
                if remove_ids:
                    remove_params = [(pid, tid) for pid in pids for tid in remove_ids]
                    await conn.executemany(
                        "DELETE FROM mod_tags WHERE package_id = ? AND tag_id = ?",
                        remove_params,
                    )
                if add_ids:
                    add_params = [(pid, tid) for pid in pids for tid in add_ids]
                    await conn.executemany(
                        "INSERT OR IGNORE INTO mod_tags (package_id, tag_id) "
                        "VALUES (?, ?)",
                        add_params,
                    )
                await conn.commit()
            except (sqlite3.IntegrityError, aiosqlite.IntegrityError) as err:
                await conn.rollback()
                raise OrganizerError(str(err)) from err
            except Exception:
                await conn.rollback()
                raise

    async def replace_rules(self, specs: Sequence[RuleSpec]) -> tuple[Rule, ...]:
        async with self._lock:
            conn = await self._get_conn()
            try:
                await conn.execute("DELETE FROM rules")
                rules: list[Rule] = []
                for pos, spec in enumerate(specs):
                    cur = await conn.execute(
                        """
                        INSERT INTO rules (position, field, op, pattern, folder_id)
                        VALUES (?, ?, ?, ?, ?)
                        """,
                        (pos, spec.field, spec.op, spec.pattern, spec.folder_id),
                    )
                    rule_id = cur.lastrowid
                    assert rule_id is not None
                    rules.append(
                        Rule(
                            id=rule_id,
                            position=pos,
                            field=spec.field,
                            op=spec.op,
                            pattern=spec.pattern,
                            folder_id=spec.folder_id,
                        )
                    )
                await conn.commit()
                return tuple(rules)
            except (sqlite3.IntegrityError, aiosqlite.IntegrityError) as err:
                await conn.rollback()
                raise OrganizerError(str(err)) from err
            except Exception:
                await conn.rollback()
                raise

    async def append_named_rules(
        self,
        parent_id: int,
        new_folders: Sequence[str],
        rules: Sequence[StandardRule],
    ) -> None:
        """Create *new_folders* under *parent_id*, then append *rules* after the
        existing ones, resolving each rule's folder by name among its children.
        """
        async with self._lock:
            conn = await self._get_conn()
            try:
                await conn.executemany(
                    "INSERT INTO folders (parent_id, name) VALUES (?, ?)",
                    [(parent_id, name) for name in new_folders],
                )
                folder_ids: dict[str, int] = {}
                async with conn.execute(
                    "SELECT id, name FROM folders WHERE parent_id = ?", (parent_id,)
                ) as cur:
                    async for row in cur:
                        folder_ids[row[1].casefold()] = row[0]
                async with conn.execute(
                    "SELECT COALESCE(MAX(position), -1) + 1 FROM rules"
                ) as cur:
                    row = await cur.fetchone()
                    assert row is not None
                    start = int(row[0])
                params: list[tuple[int, str, str, str, int]] = []
                for offset, rule in enumerate(rules):
                    folder_id = folder_ids.get(rule.folder.casefold())
                    if folder_id is None:
                        raise OrganizerError(f'Folder "{rule.folder}" does not exist.')
                    params.append(
                        (start + offset, rule.field, rule.op, rule.pattern, folder_id)
                    )
                await conn.executemany(
                    """
                    INSERT INTO rules (position, field, op, pattern, folder_id)
                    VALUES (?, ?, ?, ?, ?)
                    """,
                    params,
                )
                await conn.commit()
            except (sqlite3.IntegrityError, aiosqlite.IntegrityError) as err:
                await conn.rollback()
                raise OrganizerError(str(err)) from err
            except Exception:
                await conn.rollback()
                raise
