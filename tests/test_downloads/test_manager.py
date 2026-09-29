from __future__ import annotations

import pytest

from pxmodrim.core.downloads import DownloadManager, DownloadResult
from pxmodrim.core.downloads.downloader import Downloader
from pxmodrim.core.models.metadata.structures import ListedMod


class FakeDownloader(Downloader):
    prefix = ""

    def __init__(self) -> None:
        super().__init__()
        self._busy = False
        self.calls: list[list[str]] = []
        self.cancelled = False

    @property
    def is_downloading(self) -> bool:
        return self._busy

    @property
    def active_ids(self) -> frozenset[str]:
        return frozenset()

    def accepts(self, mod_id: str) -> bool:
        return mod_id.startswith(self.prefix)

    def updatable_id(self, mod: ListedMod) -> str | None:
        mod_id = mod.mod_path.name if mod.mod_path else ""
        return mod_id if self.accepts(mod_id) else None

    def last_synced(self, mod: ListedMod) -> float | None:
        return 1.0 if self.updatable_id(mod) else None

    async def download_mods(self, mod_ids: list[str]) -> DownloadResult:
        self.calls.append(mod_ids)
        self._busy = True
        self.busy_changed.emit(True)
        self._busy = False
        self.busy_changed.emit(False)
        result = DownloadResult(succeeded=list(mod_ids), failed=[])
        self.download_finished.emit(result)
        return result

    def cancel(self) -> None:
        self.cancelled = True


class FakeSteam(FakeDownloader):
    name = "steam"
    label = "Steam"
    prefix = "s"


class FakeGithub(FakeDownloader):
    name = "github"
    label = "Github"
    prefix = "g"


def _mod(tmp_path, name: str) -> ListedMod:
    return ListedMod(_mod_path=tmp_path / name)


def test_empty_manager_offers_nothing(tmp_path) -> None:
    manager = DownloadManager()
    assert not manager.available
    assert manager.updatable_ids([_mod(tmp_path, "s1")]) == []
    assert manager.source_of(_mod(tmp_path, "s1")) is None


async def test_download_routes_ids_to_the_accepting_downloader() -> None:
    manager = DownloadManager()
    steam, github = FakeSteam(), FakeGithub()
    manager.register(steam)
    manager.register(github)

    result = await manager.download_mods(["s1", "g1", "s2", "s1"])

    assert steam.calls == [["s1", "s2"]]
    assert github.calls == [["g1"]]
    assert sorted(result.succeeded) == ["g1", "s1", "s2"]


async def test_unclaimed_id_is_rejected_before_anything_starts() -> None:
    manager = DownloadManager()
    steam = FakeSteam()
    manager.register(steam)

    with pytest.raises(ValueError, match="x1"):
        await manager.download_mods(["s1", "x1"])
    assert steam.calls == []


def test_source_and_ids_come_from_the_owning_downloader(tmp_path) -> None:
    manager = DownloadManager()
    manager.register(FakeSteam())
    manager.register(FakeGithub())
    mods = [_mod(tmp_path, n) for n in ("s1", "g1", "other")]

    source = manager.source_of(mods[1])
    assert source is not None and source.label == "Github"
    assert manager.updatable_ids(mods) == ["s1", "g1"]
    assert manager.last_synced(mods[2]) is None


async def test_busy_reflects_any_downloader_and_cancel_stops_the_rest() -> None:
    manager = DownloadManager()
    steam, github = FakeSteam(), FakeGithub()
    manager.register(steam)
    manager.register(github)
    busy: list[bool] = []
    manager.busy_changed.connect(busy.append)

    steam.download_finished.connect(lambda _r: manager.cancel())
    await manager.download_mods(["s1", "g1"])

    assert github.calls == []
    assert busy == [True, False]
