from __future__ import annotations

import asyncio
from pathlib import Path
from typing import Any

from pxmodrim.core.checker.models import PackageId
from pxmodrim.core.config import AppConfig, ConfigService
from pxmodrim.core.context import CoreContext
from pxmodrim.core.services.diagnostics_service import (
    DATABASE_MAX_AGE_S,
    DiagnosticsService,
)


class _FakeDb:
    def __init__(
        self,
        cached: Any,
        downloaded: Any,
        gate: asyncio.Event,
        age_s: float | None = 0.0,
    ) -> None:
        self.age_s = age_s
        self.cached = cached
        self.downloaded = downloaded
        self.gate = gate
        self.downloads = 0

    def cache_age_s(self) -> float | None:
        return self.age_s

    def load_if_exists(self) -> Any:
        return self.cached

    async def ensure(self, force: bool = False) -> Any:
        self.downloads += 1
        await self.gate.wait()
        return self.downloaded


def _service(tmp_path: Path, nvw: _FakeDb, uti: _FakeDb) -> DiagnosticsService:
    service = DiagnosticsService(CoreContext(AppConfig(), ConfigService(tmp_path)))
    service._no_version_warning_service = nvw  # type: ignore[assignment]
    service._use_this_instead_service = uti  # type: ignore[assignment]
    return service


async def test_initialize_does_not_wait_for_database_download(tmp_path: Path) -> None:
    gate = asyncio.Event()
    nvw = _FakeDb(set(), {PackageId("a.b")}, gate, age_s=None)
    uti = _FakeDb({}, {}, gate, age_s=None)
    service = _service(tmp_path, nvw, uti)
    events: list[str] = []
    service.background_task_changed.connect(events.append)

    await asyncio.wait_for(service.initialize(), timeout=1)
    await asyncio.sleep(0)
    assert events == ["Downloading rule databases\u2026"]

    gate.set()
    assert service._db_task is not None
    await service._db_task
    assert events[-1] == ""
    assert PackageId("a.b") in service._checker._no_version_warning


async def test_cached_databases_are_applied_without_download(tmp_path: Path) -> None:
    gate = asyncio.Event()
    gate.set()
    nvw = _FakeDb({PackageId("x.y")}, set(), gate)
    uti = _FakeDb({"1": object()}, {}, gate)
    service = _service(tmp_path, nvw, uti)
    events: list[str] = []
    service.background_task_changed.connect(events.append)

    await service.initialize()
    await asyncio.sleep(0)

    assert PackageId("x.y") in service._checker._no_version_warning
    assert nvw.downloads == uti.downloads == 0
    assert events == []


async def test_stale_cache_is_used_immediately_and_refreshed(tmp_path: Path) -> None:
    gate = asyncio.Event()
    gate.set()
    week = DATABASE_MAX_AGE_S
    nvw = _FakeDb({PackageId("old.one")}, {PackageId("new.one")}, gate, week + 1)
    uti = _FakeDb({"1": object()}, {"2": object()}, gate, 60.0)
    service = _service(tmp_path, nvw, uti)

    await service.initialize()
    assert PackageId("old.one") in service._checker._no_version_warning
    assert service._db_task is not None
    await service._db_task

    assert (nvw.downloads, uti.downloads) == (1, 0)
    assert PackageId("new.one") in service._checker._no_version_warning


async def test_failed_refresh_keeps_cached_data(tmp_path: Path) -> None:
    gate = asyncio.Event()
    gate.set()
    nvw = _FakeDb({PackageId("old.one")}, set(), gate, DATABASE_MAX_AGE_S + 1)
    uti = _FakeDb({"1": object()}, {}, gate, 60.0)
    service = _service(tmp_path, nvw, uti)

    await service.initialize()
    assert service._db_task is not None
    await service._db_task

    assert PackageId("old.one") in service._checker._no_version_warning
