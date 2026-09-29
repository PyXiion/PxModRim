from __future__ import annotations

from pxmodrim.core.services.workshop_download_service import (
    DownloadItemStatus,
    DownloadResult,
)
from pxmodrim.ui.plugins.downloads.model import DownloadsModel


def _status(pid: str, status: str, done: int = 0, total: int = 0, error: str = ""):
    return DownloadItemStatus(pid, status, done, total, error)


def _states(model: DownloadsModel) -> dict[str, str]:
    return {r.pid: r.state for r in model._rows}


def test_states_follow_item_events_and_cancel_leftovers() -> None:
    model = DownloadsModel()
    model.begin(["1", "2", "3", "4", "5"], {"1": "One"})
    model.apply(_status("1", "downloading", 0, 0))
    model.apply(_status("2", "downloading", 5, 10))
    model.apply(_status("3", "downloading", 10, 10))
    model.apply(_status("4", "error", error="boom"))
    model._flush()
    assert _states(model) == {
        "1": "unchanged",
        "2": "downloading",
        "3": "updated",
        "4": "failed",
        "5": "queued",
    }
    model.finish(DownloadResult(succeeded=["1", "3"], failed=["4"], changed=["3"]))
    assert _states(model)["2"] == "cancelled"
    assert _states(model)["5"] == "cancelled"
    assert (model.updated, model.unchanged, model.failed, model.active) == (1, 1, 3, 0)
    assert model.failed_ids() == ["2", "4", "5"]


def test_filter_limits_visible_rows() -> None:
    model = DownloadsModel()
    model.begin(["1", "2"], {"1": "One"})
    model.apply(_status("2", "error", error="x"))
    model._flush()
    model.setFilter("failed")
    assert model.rowCount() == 1
    assert model.data(model.index(0), DownloadsModel._TitleRole) == "2"
    model.setFilter("all")
    assert model.rowCount() == 2
