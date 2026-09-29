from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Any

from PySide6.QtCore import (
    Property,
    QAbstractListModel,
    QByteArray,
    QModelIndex,
    QPersistentModelIndex,
    Qt,
    QTimer,
    Signal,
    Slot,
)

if TYPE_CHECKING:
    from pxmodrim.core.services.workshop_download_service import (
        DownloadItemStatus,
        DownloadItemTitle,
        DownloadResult,
    )

_ACTIVE = frozenset({"queued", "checking", "downloading"})
_FLUSH_MS = 150
_SPEED_TICK_MS = 1000
HISTORY_LEN = 90


@dataclass(slots=True)
class _Row:
    pid: str
    title: str
    state: str = "queued"
    progress: float = 0.0
    error: str = ""
    bytes_done: int = 0


def _final_state(bytes_total: int) -> str:
    # pxsteamdl reports 0/0 for an item that is already current.
    return "updated" if bytes_total > 0 else "unchanged"


class DownloadsModel(QAbstractListModel):
    """Per-mod state of the current or last Workshop download batch."""

    summary_changed = Signal()
    speed_changed = Signal()
    phase_changed = Signal()

    _IdRole = Qt.ItemDataRole.UserRole + 1
    _TitleRole = Qt.ItemDataRole.UserRole + 2
    _StateRole = Qt.ItemDataRole.UserRole + 3
    _ProgressRole = Qt.ItemDataRole.UserRole + 4
    _ErrorRole = Qt.ItemDataRole.UserRole + 5

    def __init__(self, parent: Any = None) -> None:
        super().__init__(parent)
        self._rows: list[_Row] = []
        self._by_id: dict[str, _Row] = {}
        self._visible: list[_Row] = []
        self._filter = "all"
        self._busy = False
        self._counts: dict[str, int] = {}
        self._dirty: set[str] = set()
        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.setInterval(_FLUSH_MS)
        self._timer.timeout.connect(self._flush)
        self._phase = ""
        self._bytes = 0
        self._sampled_bytes = 0
        self._speed = 0.0
        self._history: list[float] = []
        self._speed_timer = QTimer(self)
        self._speed_timer.setInterval(_SPEED_TICK_MS)
        self._speed_timer.timeout.connect(self._sample_speed)

    # ── batch lifecycle ───────────────────────────────

    def begin(self, ids: list[str], titles: dict[str, str]) -> None:
        self._timer.stop()
        self._dirty.clear()
        self._reset_speed()
        self.set_phase("")
        self.beginResetModel()
        self._rows = [_Row(pid, titles.get(pid, pid)) for pid in ids]
        self._by_id = {row.pid: row for row in self._rows}
        self._recount()
        self._visible = self._select()
        self.endResetModel()
        self.summary_changed.emit()

    def apply(self, status: DownloadItemStatus) -> None:
        row = self._by_id.get(status.mod_id)
        if row is None:
            return
        done = status.bytes_done if status.status != "error" else row.bytes_done
        self._bytes += done - row.bytes_done
        row.bytes_done = done
        if status.status == "error":
            row.state, row.error, row.progress = "failed", status.error, 0.0
        elif status.status == "success":
            row.state, row.progress = _final_state(status.bytes_total), 1.0
        elif status.bytes_total > 0 and status.bytes_done < status.bytes_total:
            row.state = "downloading"
            row.progress = status.bytes_done / status.bytes_total
        else:
            row.state, row.progress = _final_state(status.bytes_total), 1.0
        self._mark(row)

    def set_title(self, item: DownloadItemTitle) -> None:
        row = self._by_id.get(item.mod_id)
        if row is None:
            return
        if row.title == row.pid:
            row.title = item.title
        if row.state == "queued":
            row.state = "checking"
        self._mark(row)

    def set_phase(self, phase: str) -> None:
        if phase != self._phase:
            self._phase = phase
            self.phase_changed.emit()

    def finish(self, result: DownloadResult) -> None:
        for row in self._rows:
            if row.state in _ACTIVE:
                row.state, row.progress = "cancelled", 0.0
                self._dirty.add(row.pid)
        self._timer.stop()
        self._flush()

    def set_busy(self, busy: bool) -> None:
        if self._busy != busy:
            self._busy = busy
            if busy:
                self._sampled_bytes = self._bytes
                self._speed_timer.start()
            else:
                self._speed_timer.stop()
                self._speed = 0.0
                self.speed_changed.emit()
            self.summary_changed.emit()

    def failed_ids(self) -> list[str]:
        return [row.pid for row in self._rows if row.state in {"failed", "cancelled"}]

    @Slot()
    def clear(self) -> None:
        if not self._busy:
            self.begin([], {})

    @Slot(str)
    def setFilter(self, name: str) -> None:
        if name != self._filter:
            self._filter = name
            self.beginResetModel()
            self._visible = self._select()
            self.endResetModel()
            self.summary_changed.emit()

    # ── internals ─────────────────────────────────────

    def _reset_speed(self) -> None:
        self._bytes = self._sampled_bytes = 0
        self._speed = 0.0
        self._history = []
        self.speed_changed.emit()

    def _sample_speed(self) -> None:
        delta = self._bytes - self._sampled_bytes
        self._sampled_bytes = self._bytes
        self._speed = delta * 1000 / _SPEED_TICK_MS
        self._history = [*self._history[-(HISTORY_LEN - 1) :], self._speed]
        self.speed_changed.emit()

    def _mark(self, row: _Row) -> None:
        self._dirty.add(row.pid)
        if not self._timer.isActive():
            self._timer.start()

    def _matches(self, row: _Row) -> bool:
        return {
            "all": True,
            "active": row.state in _ACTIVE,
            "updated": row.state == "updated",
            "failed": row.state in {"failed", "cancelled"},
        }.get(self._filter, True)

    def _select(self) -> list[_Row]:
        return [row for row in self._rows if self._matches(row)]

    def _recount(self) -> None:
        counts: dict[str, int] = {}
        for row in self._rows:
            counts[row.state] = counts.get(row.state, 0) + 1
        self._counts = counts

    def _flush(self) -> None:
        if not self._dirty:
            return
        dirty, self._dirty = self._dirty, set()
        self._recount()
        if self._filter == "all":
            for pos, row in enumerate(self._visible):
                if row.pid in dirty:
                    idx = self.index(pos)
                    self.dataChanged.emit(idx, idx)
        else:
            self.beginResetModel()
            self._visible = self._select()
            self.endResetModel()
        self.summary_changed.emit()

    # ── QAbstractListModel ────────────────────────────

    def rowCount(
        self, parent: QModelIndex | QPersistentModelIndex | None = None
    ) -> int:
        return len(self._visible) if parent is None or not parent.isValid() else 0

    def data(
        self,
        index: QModelIndex | QPersistentModelIndex,
        role: int = Qt.ItemDataRole.DisplayRole,
    ) -> Any:
        if not index.isValid() or index.row() >= len(self._visible):
            return None
        row = self._visible[index.row()]
        if role == self._IdRole:
            return row.pid
        if role == self._TitleRole:
            return row.title
        if role == self._StateRole:
            return row.state
        if role == self._ProgressRole:
            return row.progress
        if role == self._ErrorRole:
            return row.error
        return None

    def roleNames(self) -> dict[int, QByteArray]:
        return {
            self._IdRole: QByteArray(b"id"),
            self._TitleRole: QByteArray(b"title"),
            self._StateRole: QByteArray(b"state"),
            self._ProgressRole: QByteArray(b"progress"),
            self._ErrorRole: QByteArray(b"error"),
        }

    # ── QML summary ───────────────────────────────────

    def _count(self, *states: str) -> int:
        return sum(self._counts.get(s, 0) for s in states)

    @Property(int, notify=summary_changed)  # type: ignore[arg-type]
    def total(self) -> int:
        return len(self._rows)

    @Property(int, notify=summary_changed)  # type: ignore[arg-type]
    def finished(self) -> int:
        return self._count("updated", "unchanged", "failed", "cancelled")

    @Property(int, notify=summary_changed)  # type: ignore[arg-type]
    def updated(self) -> int:
        return self._count("updated")

    @Property(int, notify=summary_changed)  # type: ignore[arg-type]
    def unchanged(self) -> int:
        return self._count("unchanged")

    @Property(int, notify=summary_changed)  # type: ignore[arg-type]
    def failed(self) -> int:
        return self._count("failed", "cancelled")

    @Property(int, notify=summary_changed)  # type: ignore[arg-type]
    def active(self) -> int:
        return self._count("queued", "checking", "downloading")

    @Property(str, notify=phase_changed)  # type: ignore[arg-type]
    def phase(self) -> str:
        return self._phase

    @Property(int, notify=summary_changed)  # type: ignore[arg-type]
    def resolved(self) -> int:
        return len(self._rows) - self._count("queued")

    @Property(float, notify=speed_changed)  # type: ignore[arg-type]
    def speed(self) -> float:
        return self._speed

    @Property(float, notify=speed_changed)  # type: ignore[arg-type]
    def bytesDone(self) -> float:
        return float(self._bytes)

    @Property(list, notify=speed_changed)  # type: ignore[arg-type]
    def speedHistory(self) -> list[float]:
        return self._history

    @Property(bool, notify=summary_changed)  # type: ignore[arg-type]
    def busy(self) -> bool:
        return self._busy

    @Property(str, notify=summary_changed)  # type: ignore[arg-type]
    def filter(self) -> str:
        return self._filter
