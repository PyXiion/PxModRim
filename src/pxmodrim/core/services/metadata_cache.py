from __future__ import annotations

import asyncio
import contextlib
import json
import sqlite3
from pathlib import Path
from typing import Any

import aiosqlite
from loguru import logger

from pxmodrim.core.migrator import ensure_schema
from pxmodrim.core.models.metadata.structures import (
    AboutXmlMod,
    BaseRules,
    CaseInsensitiveSet,
    CaseInsensitiveStr,
    DependencyMod,
)

_SCHEMA = """
    CREATE TABLE IF NOT EXISTS mod_metadata (
        mod_path        TEXT PRIMARY KEY,
        about_path      TEXT NOT NULL,
        mtime           REAL NOT NULL,
        file_size       INTEGER NOT NULL DEFAULT 0,
        target_version  TEXT NOT NULL,
        payload_json    TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_mm_about
        ON mod_metadata(about_path);
"""

_SCHEMA_VERSION = 1


def metadata_cache_path(config_dir: Path) -> Path:
    return config_dir / "metadata-cache.db"


def normalize_path(path: Path | str) -> str:
    return str(Path(path).resolve())


def serialize_mod(mod: AboutXmlMod) -> dict[str, Any]:
    return {
        "name": mod.name,
        "package_id": str(mod.package_id),
        "authors": list(mod.authors),
        "mod_version": mod.mod_version,
        "mod_icon_path": str(mod.mod_icon_path) if mod.mod_icon_path else None,
        "steam_app_id": mod.steam_app_id,
        "url": mod.url,
        "description": mod.description,
        "supported_versions": sorted(mod.supported_versions),
        "valid": mod.valid,
        "mtime": mod.mtime,
        "about_rules": {
            "load_before": sorted(str(x) for x in mod.about_rules.load_before),
            "load_after": sorted(str(x) for x in mod.about_rules.load_after),
            "incompatible_with": sorted(
                str(x) for x in mod.about_rules.incompatible_with
            ),
            "dependencies": {
                str(k): {
                    "name": d.name,
                    "package_id": str(d.package_id),
                    "workshop_url": d.workshop_url,
                    "alternative_package_ids": sorted(
                        str(a) for a in d.alternative_package_ids
                    ),
                }
                for k, d in mod.about_rules.dependencies.items()
            },
        },
    }


def deserialize_mod(mod_path: Path, data: dict[str, Any]) -> AboutXmlMod:
    mod = AboutXmlMod()
    mod.mod_path = mod_path
    mod.name = data.get("name", "Unknown Mod Name")
    mod.package_id = CaseInsensitiveStr(data.get("package_id", "invalid.mod"))
    mod.authors = list(data.get("authors", []))
    mod.mod_version = data.get("mod_version", "")
    icon_str = data.get("mod_icon_path")
    mod.mod_icon_path = Path(icon_str) if icon_str else None
    mod.steam_app_id = data.get("steam_app_id", -1)
    mod.url = data.get("url", "")
    mod.description = data.get("description", "")
    mod.supported_versions = set(data.get("supported_versions", []))
    mod.valid = data.get("valid", True)
    mod.mtime = data.get("mtime", 0.0)

    rules_data = data.get("about_rules", {})
    rules = BaseRules()
    rules.load_before = CaseInsensitiveSet(rules_data.get("load_before", []))
    rules.load_after = CaseInsensitiveSet(rules_data.get("load_after", []))
    rules.incompatible_with = CaseInsensitiveSet(
        rules_data.get("incompatible_with", [])
    )
    deps: dict[CaseInsensitiveStr, DependencyMod] = {}
    for k, d in rules_data.get("dependencies", {}).items():
        dep_pid = CaseInsensitiveStr(d.get("package_id", k))
        deps[dep_pid] = DependencyMod(
            name=d.get("name", ""),
            package_id=dep_pid,
            workshop_url=d.get("workshop_url", ""),
            alternative_package_ids={
                CaseInsensitiveStr(a) for a in d.get("alternative_package_ids", [])
            },
        )
    rules.dependencies = deps
    mod.about_rules = rules
    return mod


class MetadataCache:
    """Persistent SQLite cache for parsed About.xml metadata.

    Owns a dedicated metadata-cache.db aiosqlite connection guarded by an asyncio lock.
    """

    __slots__ = (
        "_connection",
        "_db_path",
        "_disabled",
        "_lock",
    )

    def __init__(self, path: Path) -> None:
        self._db_path = path
        self._connection: aiosqlite.Connection | None = None
        self._disabled: bool = False
        self._lock = asyncio.Lock()

    async def _get_connection(self) -> aiosqlite.Connection | None:
        if self._disabled:
            return None

        async with self._lock:
            if self._connection is not None:
                return self._connection

            self._db_path.parent.mkdir(parents=True, exist_ok=True)
            db_str = str(self._db_path)
            try:
                conn = await self._open_and_init(db_str)
                self._connection = conn
                return conn
            except (sqlite3.DatabaseError, aiosqlite.DatabaseError) as exc:
                logger.warning(
                    "Corrupt metadata cache database at {}: {}. Attempting recovery.",
                    self._db_path,
                    exc,
                )
                await self._recover_corrupt_db()
                try:
                    conn = await self._open_and_init(db_str)
                    self._connection = conn
                    return conn
                except (sqlite3.Error, aiosqlite.Error, OSError) as recover_exc:
                    logger.error(
                        "Failed to re-initialize metadata cache at {}: {}. "
                        "Disabling cache.",
                        self._db_path,
                        recover_exc,
                    )
                    self._disabled = True
                    return None
            except OSError as exc:
                logger.warning(
                    "Metadata cache unavailable at {}: {}. Disabling cache.",
                    self._db_path,
                    exc,
                )
                self._disabled = True
                return None

    async def _open_and_init(self, db_str: str) -> aiosqlite.Connection:
        conn = await aiosqlite.connect(db_str, check_same_thread=False)
        try:
            await conn.execute("PRAGMA journal_mode=WAL")
            await conn.execute("PRAGMA busy_timeout=5000")
            await conn.execute("PRAGMA foreign_keys=ON")
            await conn.execute("PRAGMA auto_vacuum=INCREMENTAL")
            await ensure_schema(
                conn,
                schema_sql=_SCHEMA,
                schema_version=_SCHEMA_VERSION,
                steps={},
                backup_path=self._db_path,
                label="metadata cache schema",
            )
            return conn
        except BaseException:
            await conn.close()
            raise

    async def _recover_corrupt_db(self) -> None:
        def _remove() -> None:
            for p in (
                self._db_path,
                self._db_path.with_name(f"{self._db_path.name}-wal"),
                self._db_path.with_name(f"{self._db_path.name}-shm"),
            ):
                with contextlib.suppress(OSError):
                    if p.exists():
                        p.unlink()

        await asyncio.to_thread(_remove)

    async def close(self) -> None:
        async with self._lock:
            if self._connection is not None:
                await self._connection.close()
                self._connection = None

    def close_sync(self) -> None:
        if self._connection is not None:
            self._connection._conn.close()
            self._connection = None

    async def get_mods(
        self,
        candidates: list[tuple[Path, Path, float, int]],
        target_version: str,
    ) -> tuple[dict[Path, AboutXmlMod], list[tuple[Path, Path, float, int]]]:
        """Look up candidate mods in cache.

        Returns (cached_mods_by_path, missing_or_stale_candidates).
        """
        if not candidates:
            return {}, []
        conn = await self._get_connection()
        if conn is None:
            return {}, list(candidates)

        norm_map = {normalize_path(c[0]): c for c in candidates}
        cached_mods: dict[Path, AboutXmlMod] = {}
        missing: list[tuple[Path, Path, float, int]] = []

        try:
            async with self._lock:
                rows = await self._fetch_candidates(conn, list(norm_map.keys()))

            found_norms: set[str] = set()
            for row in rows:
                norm_p, _about_p, mtime, file_size, ver, payload = row
                cand = norm_map.get(norm_p)
                if cand is None:
                    continue
                orig_mod_path, _orig_about_path, cur_mtime, cur_size = cand
                if (
                    abs(mtime - cur_mtime) < 1e-4
                    and file_size == cur_size
                    and ver == target_version
                ):
                    try:
                        data = json.loads(payload)
                        mod = deserialize_mod(orig_mod_path, data)
                        cached_mods[orig_mod_path] = mod
                        found_norms.add(norm_p)
                    except (json.JSONDecodeError, KeyError) as e:
                        logger.warning(
                            "Corrupted metadata cache payload for {}: {}", norm_p, e
                        )

            for norm_p, cand in norm_map.items():
                if norm_p not in found_norms:
                    missing.append(cand)

            return cached_mods, missing
        except (sqlite3.Error, aiosqlite.Error, OSError) as exc:
            logger.warning("Error reading metadata cache: {}", exc)
            return {}, list(candidates)

    async def _fetch_candidates(
        self, conn: aiosqlite.Connection, norm_paths: list[str]
    ) -> list[Any]:
        rows: list[Any] = []
        chunk_size = 500
        for i in range(0, len(norm_paths), chunk_size):
            chunk = norm_paths[i : i + chunk_size]
            placeholders = ",".join("?" for _ in chunk)
            query = (
                "SELECT mod_path, about_path, mtime, file_size, target_version, "
                f"payload_json FROM mod_metadata WHERE mod_path IN ({placeholders})"
            )
            async with conn.execute(query, chunk) as cursor:
                fetched = await cursor.fetchall()
                rows.extend(fetched)
        return rows

    async def put_mods(
        self,
        entries: list[tuple[AboutXmlMod, Path, float, int]],
        target_version: str,
    ) -> None:
        """Store or update parsed metadata entries in the cache."""
        if not entries:
            return
        conn = await self._get_connection()
        if conn is None:
            return

        rows = []
        for mod, about_path, mtime, file_size in entries:
            if mod.mod_path is None:
                continue
            norm_mod_path = normalize_path(mod.mod_path)
            norm_about_path = normalize_path(about_path)
            payload = json.dumps(serialize_mod(mod))
            rows.append(
                (
                    norm_mod_path,
                    norm_about_path,
                    mtime,
                    file_size,
                    target_version,
                    payload,
                )
            )

        try:
            async with self._lock:
                await conn.executemany(
                    "INSERT OR REPLACE INTO mod_metadata "
                    "(mod_path, about_path, mtime, file_size, target_version, "
                    "payload_json) VALUES (?, ?, ?, ?, ?, ?)",
                    rows,
                )
                await conn.commit()
        except (sqlite3.Error, aiosqlite.Error, OSError) as exc:
            logger.warning("Error writing to metadata cache: {}", exc)

    async def prune_disappeared(self, live_mod_paths: set[str]) -> None:
        """Remove entries for mods that no longer exist on disk."""
        conn = await self._get_connection()
        if conn is None:
            return

        try:
            async with self._lock:
                async with conn.execute("SELECT mod_path FROM mod_metadata") as cursor:
                    cached_rows = await cursor.fetchall()
                cached_paths = {row[0] for row in cached_rows}
                disappeared = cached_paths - live_mod_paths
                if disappeared:
                    logger.debug(
                        "Pruning {} disappeared mods from metadata cache",
                        len(disappeared),
                    )
                    await conn.executemany(
                        "DELETE FROM mod_metadata WHERE mod_path = ?",
                        [(p,) for p in disappeared],
                    )
                    await conn.commit()
        except (sqlite3.Error, aiosqlite.Error, OSError) as exc:
            logger.warning("Error pruning metadata cache: {}", exc)
