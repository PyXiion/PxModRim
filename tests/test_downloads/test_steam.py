from __future__ import annotations

import asyncio
import threading
from collections.abc import Callable, Sequence
from os import PathLike
from pathlib import Path
from types import SimpleNamespace
from typing import Any

import pxsteamdl
import pytest

from pxmodrim.core.config import AppConfig, ConfigService
from pxmodrim.core.context import CoreContext
from pxmodrim.core.downloads import DownloadItemStatus, DownloadResult
from pxmodrim.core.downloads.manager import download_manager
from pxmodrim.core.downloads.steam import (
    ClientOptions,
    SteamDownloader,
    SteamSettings,
    WorkshopSyncState,
)
from pxmodrim.core.mod_service import ModService
from pxmodrim.core.models.metadata.structures import ListedMod
from pxmodrim.core.workshop import DownloadPlan, WorkshopCatalog


class FakeClient:
    def __init__(
        self,
        errors: dict[int, str] | None = None,
        gates: dict[int, threading.Event] | None = None,
        network_failures: frozenset[int] = frozenset(),
    ) -> None:
        self.errors = errors or {}
        self.gates = gates or {}
        self.network_failures = network_failures
        self.stopped = False
        self.calls: list[tuple[list[int], Path, int]] = []
        self.blocked = threading.Event()

    def download(
        self,
        ids: Sequence[int],
        root: str | PathLike[str],
        *,
        parallel_items: int = 4,
        threads_per_item: int = 8,
        on_progress: Callable[[Any], None] | None = None,
        on_resolved: Callable[[Any], None] | None = None,
        cancel: Any = None,
    ) -> list[Any]:
        self.calls.append((list(ids), Path(root), parallel_items * threads_per_item))
        results = []
        for item_id in ids:
            if on_resolved is not None:
                on_resolved(
                    SimpleNamespace(item_id=item_id, title=f"Title {item_id}", error="")
                )
            if on_progress is not None:
                on_progress(
                    SimpleNamespace(
                        item_id=item_id, unpacked_bytes=5, unpacked_total=10
                    )
                )
            gate = self.gates.get(item_id)
            if gate is not None:
                self.blocked.set()
                gate.wait(timeout=5)
            error = "cancelled" if self.stopped else self.errors.get(item_id, "")
            if not error:
                (Path(root) / str(item_id)).mkdir(parents=True, exist_ok=True)
            results.append(
                SimpleNamespace(
                    item_id=item_id,
                    error=error,
                    cancelled=self.stopped,
                    error_kind=(
                        pxsteamdl.ErrorKind.NETWORK
                        if item_id in self.network_failures
                        else pxsteamdl.ErrorKind.NONE
                    ),
                )
            )
        return results


def _cfg(tmp_path: Path, *, local: bool = True) -> AppConfig:
    cfg = AppConfig()
    if local:
        cfg.paths.local = str(tmp_path / "Mods")
    return cfg


def _service(
    tmp_path: Path, client: FakeClient | None = None, *, local: bool = True
) -> tuple[SteamDownloader, list[ClientOptions]]:
    cfg = _cfg(tmp_path, local=local)
    logins: list[ClientOptions] = []

    async def factory(options: ClientOptions) -> FakeClient:
        logins.append(options)
        if client is None:
            raise RuntimeError("logon denied")
        return client

    svc = SteamDownloader(factory)
    ctx = CoreContext(cfg, ConfigService(tmp_path))
    svc.setup(ctx)
    return svc, logins


async def test_login_uses_configured_proxy_and_timeouts(tmp_path: Path) -> None:
    svc, logins = _service(tmp_path, FakeClient())
    assert svc._ctx is not None
    await svc.download_mods(["111"])
    svc.settings.update(
        SteamSettings(proxy=" socks5h://host:1080 ", connect_timeout=3, stall_timeout=7)
    )
    await svc.download_mods(["222"])
    assert logins == [(None, 10, 30), ("socks5h://host:1080", 3, 7)]


async def test_network_failure_triggers_new_login(tmp_path: Path) -> None:
    svc, logins = _service(tmp_path, FakeClient(network_failures=frozenset({111})))
    await svc.download_mods(["111"])
    await svc.download_mods(["222"])
    assert len(logins) == 2


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
        while not client.blocked.is_set():
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


def _synced(tmp_path: Path, svc: SteamDownloader, pid: str) -> float | None:
    return svc.last_synced(ListedMod(_mod_path=tmp_path / "Mods" / pid))


def _load(svc: SteamDownloader, mods: list[ListedMod]) -> None:
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
    assert svc._ctx is not None
    assert svc.updatable_ids(svc._ctx.all_mods.values()) == ["111", "333"]


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
    from pxmodrim.core.downloads.steam import _Batch

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
    from pxmodrim.core.downloads.steam import _Batch

    batch = _Batch(2)
    batch.succeeded = ["1", "2"]
    batch.bytes = {"1": (0, 0), "2": (5, 5)}
    assert batch.changed() == ["2"]


async def test_success_records_sync_time_and_persists(tmp_path: Path) -> None:
    svc, _ = _service(tmp_path, FakeClient(errors={222: "gone"}))
    await svc.download_mods(["111", "222"])
    assert _synced(tmp_path, svc, "111") is not None
    assert _synced(tmp_path, svc, "222") is None
    reloaded = ConfigService(tmp_path).load("workshop_sync.json", WorkshopSyncState)
    assert set(reloaded.synced) == {"111"}


def test_stale_ids_never_synced_first_then_oldest(tmp_path: Path) -> None:
    svc, _ = _service(tmp_path)
    mods = [_mod_at(tmp_path / "Mods" / n, n) for n in ("1", "2", "3", "4")]
    _load(svc, mods)
    svc._sync.synced.update({"1": 1000.0, "2": 9000.0, "3": 500.0})
    assert svc.stale_ids(3600, now=10000.0) == ["4", "3", "1"]


async def test_auto_update_only_syncs_stale_mods_when_enabled(tmp_path: Path) -> None:
    client = FakeClient()
    svc, _ = _service(tmp_path, client)
    assert svc._ctx is not None
    _load(svc, [_mod_at(tmp_path / "Mods" / "1", "1")])
    await svc._auto_update_once()
    assert _synced(tmp_path, svc, "1") is None

    svc.settings.update(SteamSettings(auto_update_hours=6))
    await svc._auto_update_once()
    assert _synced(tmp_path, svc, "1") is not None
    synced = _synced(tmp_path, svc, "1")
    await svc._auto_update_once()
    assert _synced(tmp_path, svc, "1") == synced


@pytest.mark.parametrize("cancel_auto_update", [False, True])
async def test_cancelled_manual_batch_does_not_discard_queue_after_auto_update(
    tmp_path: Path,
    cancel_auto_update: bool,
) -> None:
    manual_gate = threading.Event()
    auto_gate = threading.Event()
    client = FakeClient(gates={111: manual_gate, 1: auto_gate})
    svc, _ = _service(tmp_path, client)
    assert svc._ctx is not None
    ctx = svc._ctx
    ctx._mod_service = ModService(ctx, [])
    _load(svc, [_mod_at(tmp_path / "Mods" / "1", "1")])
    svc.settings.update(SteamSettings(auto_update_hours=6))
    manager = download_manager(ctx)
    catalog = WorkshopCatalog()
    catalog.setup(ctx)
    tasks: list[asyncio.Task[Any]] = []
    try:
        manual = asyncio.create_task(manager.download_mods(["111"]))
        tasks.append(manual)
        assert await asyncio.to_thread(client.blocked.wait, 5)
        manager.cancel()
        client.stopped = True
        manual_gate.set()
        assert (await asyncio.wait_for(manual, 5)).succeeded == []
        assert manager.cancelled

        client.stopped = False
        client.blocked.clear()
        auto_update = asyncio.create_task(svc._auto_update_once())
        tasks.append(auto_update)
        assert await asyncio.to_thread(client.blocked.wait, 5)
        assert manager.is_downloading
        catalog.enqueue(DownloadPlan(["222"], [], [], [], {}, True))
        runner = catalog._runner
        assert runner is not None
        await asyncio.sleep(0)
        assert not runner.done()
        assert catalog.queued_ids == {"222"}
        if cancel_auto_update:
            manager.cancel()
            client.stopped = True

        auto_gate.set()
        await asyncio.wait_for(auto_update, 5)
        await asyncio.wait_for(runner, 5)
        assert not catalog.queued_ids
        if cancel_auto_update:
            assert [call[0] for call in client.calls] == [[111], [1]]
            assert manager.cancelled
            assert _synced(tmp_path, svc, "222") is None
        else:
            assert [call[0] for call in client.calls] == [[111], [1], [222]]
            assert not manager.cancelled
            assert _synced(tmp_path, svc, "222") is not None
    finally:
        manual_gate.set()
        auto_gate.set()
        await asyncio.gather(*tasks, return_exceptions=True)
        await catalog.shutdown()
        await svc.shutdown()


async def test_titles_from_steam_are_emitted(tmp_path: Path) -> None:
    svc, _ = _service(tmp_path, FakeClient())
    titles: dict[str, str] = {}
    svc.download_item_titled.connect(lambda t: titles.update({t.mod_id: t.title}))

    phases: list[str] = []
    svc.download_phase_changed.connect(phases.append)

    await svc.download_mods(["111", "222"])

    assert phases == ["login", "query", "run"]
    assert titles == {"111": "Title 111", "222": "Title 222"}


async def test_client_is_relogged_after_download_raises(tmp_path: Path) -> None:
    class Dying(FakeClient):
        def download(self, ids, root, **kwargs):  # type: ignore[no-untyped-def]
            raise RuntimeError("session lost")

    clients: list[FakeClient] = [Dying(), FakeClient()]
    logins: list[int] = []

    async def factory(_options: ClientOptions) -> FakeClient:
        logins.append(1)
        return clients[len(logins) - 1]

    svc = SteamDownloader(factory)
    svc.setup(CoreContext(_cfg(tmp_path), ConfigService(tmp_path)))
    await svc.download_mods(["1"])
    await svc.download_mods(["2"])
    assert len(logins) == 2


async def test_auto_update_loop_survives_unexpected_error(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr("pxmodrim.core.downloads.steam.AUTO_STARTUP_DELAY_S", 0)
    monkeypatch.setattr("pxmodrim.core.downloads.steam.AUTO_CHECK_INTERVAL_S", 0)
    svc, _ = _service(tmp_path, FakeClient())
    calls = 0

    async def flaky() -> None:
        nonlocal calls
        calls += 1
        if calls == 1:
            raise KeyError("boom")

    monkeypatch.setattr(svc, "_auto_update_once", flaky)
    task = asyncio.create_task(svc._auto_update_loop())
    for _ in range(20):
        await asyncio.sleep(0)
    task.cancel()
    assert calls >= 2


async def test_download_uses_configured_parallelism_clamped(tmp_path: Path) -> None:
    client = FakeClient()
    svc, _ = _service(tmp_path, client)
    assert svc._ctx is not None
    svc.settings.update(SteamSettings(parallel_items=3, threads_per_item=5))
    await svc.download_mods(["1"])
    svc.settings.update(SteamSettings(parallel_items=0, threads_per_item=999))
    await svc.download_mods(["2"])
    assert [c[2] for c in client.calls] == [15, 16]
