from __future__ import annotations

import asyncio
import threading
from collections.abc import Callable, Sequence
from os import PathLike
from pathlib import Path
from types import SimpleNamespace
from typing import Any

import pytest

from pxmodrim.core.config import AppConfig, ConfigService
from pxmodrim.core.context import CoreContext
from pxmodrim.core.models.metadata.structures import ListedMod
from pxmodrim.core.services.workshop_download_service import (
    DownloadItemStatus,
    DownloadResult,
    WorkshopDownloadService,
)


class FakeClient:
    def __init__(
        self,
        errors: dict[int, str] | None = None,
        gates: dict[int, threading.Event] | None = None,
    ) -> None:
        self.errors = errors or {}
        self.gates = gates or {}
        self.stopped = False
        self.calls: list[tuple[list[int], Path, int]] = []

    def download(
        self,
        ids: Sequence[int],
        root: str | PathLike[str],
        *,
        parallel_items: int = 4,
        threads_per_item: int = 8,
        on_progress: Callable[[Any], None] | None = None,
        cancel: Any = None,
    ) -> list[Any]:
        self.calls.append((list(ids), Path(root), parallel_items * threads_per_item))
        results = []
        for item_id in ids:
            if on_progress is not None:
                on_progress(
                    SimpleNamespace(item_id=item_id, bytes_done=5, bytes_total=10)
                )
            gate = self.gates.get(item_id)
            if gate is not None:
                gate.wait(timeout=5)
            error = "cancelled" if self.stopped else self.errors.get(item_id, "")
            if not error:
                (Path(root) / str(item_id)).mkdir(parents=True, exist_ok=True)
            results.append(SimpleNamespace(item_id=item_id, error=error))
        return results


def _service(
    tmp_path: Path, client: FakeClient | None = None, *, local: bool = True
) -> tuple[WorkshopDownloadService, list[int]]:
    cfg = AppConfig()
    if local:
        cfg.paths.local = str(tmp_path / "Mods")
    logins: list[int] = []

    async def factory() -> FakeClient:
        logins.append(1)
        if client is None:
            raise RuntimeError("logon denied")
        return client

    svc = WorkshopDownloadService(factory)
    svc.setup(CoreContext(cfg, ConfigService(tmp_path)))
    return svc, logins


async def test_downloads_into_local_mods_and_splits_results(tmp_path: Path) -> None:
    client = FakeClient(errors={222: "item not found"})
    svc, _ = _service(tmp_path, client)
    statuses: list[DownloadItemStatus] = []
    finished: list[DownloadResult] = []
    svc.download_item_status_changed.connect(statuses.append)
    svc.download_finished.connect(finished.append)

    result = await svc.download_mods(["111", "222", "111"])

    assert sorted(result.succeeded) == ["111"]
    assert result.failed == ["222"]
    assert finished == [result]
    assert (tmp_path / "Mods" / "111").is_dir()
    assert client.calls[0][0] == [111, 222]
    assert {call[1] for call in client.calls} == {tmp_path / "Mods"}
    final = {s.mod_id: s for s in statuses if s.status != "downloading"}
    assert final["111"].status == "success"
    assert final["222"].status == "error"
    assert final["222"].error == "item not found"


@pytest.mark.parametrize(
    "ids", [["12a"], ["١٢٣"], ["1 2"], [], ["18446744073709551616"]]
)
async def test_rejects_invalid_ids(tmp_path: Path, ids: list[str]) -> None:
    svc, logins = _service(tmp_path, FakeClient())
    with pytest.raises(ValueError):
        await svc.download_mods(ids)
    assert logins == []


async def test_requires_local_mods_path(tmp_path: Path) -> None:
    svc, logins = _service(tmp_path, FakeClient(), local=False)
    with pytest.raises(ValueError, match="Local mods path"):
        await svc.download_mods(["111"])
    assert logins == []


async def test_accepts_uint64_max_id(tmp_path: Path) -> None:
    client = FakeClient()
    svc, _ = _service(tmp_path, client)
    result = await svc.download_mods(["18446744073709551615"])
    assert result.succeeded == ["18446744073709551615"]
    assert client.calls[0][0] == [2**64 - 1]


async def test_unwritable_local_path_fails_every_item(tmp_path: Path) -> None:
    svc, logins = _service(tmp_path, FakeClient())
    (tmp_path / "Mods").write_text("not a directory")
    result = await svc.download_mods(["111", "222"])
    assert sorted(result.failed) == ["111", "222"]
    assert logins == []


async def test_login_failure_fails_every_item_and_retries_next_time(
    tmp_path: Path,
) -> None:
    svc, logins = _service(tmp_path, None)
    messages: list[str] = []
    svc.status_message_changed.connect(messages.append)

    result = await svc.download_mods(["111", "222"])
    await svc.download_mods(["111"])

    assert result.succeeded == []
    assert sorted(result.failed) == ["111", "222"]
    assert any("logon denied" in m for m in messages)
    assert len(logins) == 2


async def test_client_session_is_reused(tmp_path: Path) -> None:
    svc, logins = _service(tmp_path, FakeClient())
    await svc.download_mods(["111"])
    await svc.download_mods(["222"])
    assert len(logins) == 1


async def test_uses_at_most_eight_download_workers(tmp_path: Path) -> None:
    client = FakeClient()
    svc, _ = _service(tmp_path, client)
    await svc.download_mods([str(i) for i in range(1, 20)])
    assert len(client.calls) == 1
    assert client.calls[0][2] <= 8


async def test_cancel_keeps_finished_items_and_returns(tmp_path: Path) -> None:
    gate = threading.Event()
    client = FakeClient(gates={222: gate})
    svc, _ = _service(tmp_path, client)
    finished: list[DownloadResult] = []
    svc.download_finished.connect(finished.append)

    async def cancel_when_blocked() -> None:
        while not (tmp_path / "Mods" / "111").exists():
            await asyncio.sleep(0.01)
        svc.cancel()
        client.stopped = True
        gate.set()

    canceller = asyncio.create_task(cancel_when_blocked())
    result = await svc.download_mods(["111", "222", "333"])
    await canceller

    assert result.succeeded == ["111"]
    assert result.failed == []
    assert finished == [result]
    assert not svc.is_downloading


async def test_second_download_while_running_is_rejected(tmp_path: Path) -> None:
    gate = threading.Event()
    svc, _ = _service(tmp_path, FakeClient(gates={111: gate}))
    first = asyncio.create_task(svc.download_mods(["111"]))
    while not svc.is_downloading:
        await asyncio.sleep(0)

    with pytest.raises(RuntimeError, match="already running"):
        await svc.download_mods(["222"])

    gate.set()
    assert (await first).succeeded == ["111"]


def _mod_at(path: Path, pfid: str | None = None) -> ListedMod:
    if pfid is not None:
        (path / "About").mkdir(parents=True)
        (path / "About" / "PublishedFileId.txt").write_text(pfid)
    else:
        path.mkdir(parents=True)
    return ListedMod(_mod_path=path)


def _load(svc: WorkshopDownloadService, mods: list[ListedMod]) -> None:
    assert svc._ctx is not None
    svc._ctx.load({m.uuid: m for m in mods}, [])


def test_updatable_id_only_for_downloads_in_local_mods(tmp_path: Path) -> None:
    svc, _ = _service(tmp_path)
    mods = [
        _mod_at(tmp_path / "Mods" / "111", "111"),
        _mod_at(tmp_path / "Mods" / "Renamed", "222"),
        _mod_at(tmp_path / "Mods" / "333"),
        _mod_at(tmp_path / "Workshop" / "444", "444"),
    ]
    _load(svc, mods)
    assert [svc.updatable_id(m) for m in mods] == ["111", None, "333", None]
    assert svc.updatable_ids() == ["111", "333"]


def test_updatable_id_none_without_local_path(tmp_path: Path) -> None:
    svc, _ = _service(tmp_path, local=False)
    assert svc.updatable_id(_mod_at(tmp_path / "Mods" / "111", "111")) is None


async def test_busy_changed_brackets_download(tmp_path: Path) -> None:
    svc, _ = _service(tmp_path, FakeClient())
    events: list[object] = []
    svc.busy_changed.connect(lambda busy: events.append(("busy", busy)))
    svc.download_finished.connect(lambda _r: events.append("finished"))
    await svc.download_mods(["111"])
    assert events == [("busy", True), ("busy", False), "finished"]


async def test_invalid_ids_do_not_toggle_busy(tmp_path: Path) -> None:
    svc, _ = _service(tmp_path, FakeClient())
    busy: list[bool] = []
    svc.busy_changed.connect(busy.append)
    with pytest.raises(ValueError):
        await svc.download_mods(["12a"])
    assert busy == []


def test_progress_counts_complete_and_up_to_date_items() -> None:
    from pxmodrim.core.services.workshop_download_service import _Batch

    batch = _Batch(3)
    batch.bytes = {"1": (10, 10), "2": (5, 10), "3": (0, 0)}
    assert batch.progress().completed == 2
    batch.succeeded.append("1")
    assert batch.progress().completed == 2


def test_updatable_ids_limited_to_given_mods_and_deduplicated(tmp_path: Path) -> None:
    svc, _ = _service(tmp_path)
    a = _mod_at(tmp_path / "Mods" / "111", "111")
    b = _mod_at(tmp_path / "Mods" / "222", "222")
    _load(svc, [a, b])
    assert svc.updatable_ids([b, b, _mod_at(tmp_path / "Other" / "9", "9")]) == ["222"]


async def test_result_changed_excludes_up_to_date_items(tmp_path: Path) -> None:
    svc, _ = _service(tmp_path, FakeClient())
    assert svc.active_ids == frozenset()
    result = await svc.download_mods(["111"])
    assert result.changed == ["111"]
    assert svc.active_ids == frozenset()


def test_batch_changed_ignores_zero_byte_items() -> None:
    from pxmodrim.core.services.workshop_download_service import _Batch

    batch = _Batch(2)
    batch.succeeded = ["1", "2"]
    batch.bytes = {"1": (0, 0), "2": (5, 5)}
    assert batch.changed() == ["2"]
