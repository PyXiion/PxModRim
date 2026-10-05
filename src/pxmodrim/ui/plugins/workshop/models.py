from __future__ import annotations

import html
import re
from collections.abc import Callable, Sequence
from typing import Any
from urllib.parse import urlsplit

from PySide6.QtCore import (
    Property,
    QAbstractListModel,
    QByteArray,
    QModelIndex,
    QObject,
    QPersistentModelIndex,
    Qt,
    Signal,
)

from pxmodrim.core.workshop import CatalogCollection, CatalogMod, InstallState

_TOKEN = re.compile(
    r"\[(/?)(b|i|u|h[1-3]|list|\*|url|img)(?:=([^\]]*))?\]", re.IGNORECASE
)


def safe_url(value: str) -> str:
    if any(char.isspace() or ord(char) < 32 for char in value):
        return ""
    try:
        parsed = urlsplit(value)
    except ValueError:
        return ""
    return value if parsed.scheme.lower() in {"http", "https"} and parsed.netloc else ""


def description_rich_text(text: str, description_format: str = "bbcode") -> str:
    """Convert untrusted descriptions to the small Qt rich-text subset we emit."""
    if description_format == "text":
        return html.escape(text).replace("\n", "<br>")
    result: list[str] = []
    stack: list[tuple[str, str]] = []
    position = 0
    while match := _TOKEN.search(text, position):
        result.append(html.escape(text[position : match.start()]).replace("\n", "<br>"))
        closing, tag, argument = match.groups()
        tag = tag.lower()
        position = match.end()
        if not closing and tag in {"url", "img"}:
            end = re.search(rf"\[/{tag}\]", text[position:], re.IGNORECASE)
            if end is None:
                result.append(html.escape(match.group()))
                continue
            body = text[position : position + end.start()]
            url = safe_url(argument if argument is not None else body)
            label = html.escape(body if tag == "url" else "Image")
            result.append(
                f'<a href="{html.escape(url, quote=True)}">{label}</a>'
                if url
                else label
            )
            position += end.end()
        elif closing:
            if stack and stack[-1][0] == "*" and tag == "list":
                result.append(stack.pop()[1])
            if stack and stack[-1][0] == tag:
                result.append(stack.pop()[1])
            else:
                result.append(html.escape(match.group()))
        elif tag == "*":
            if stack and stack[-1][0] == "*":
                result.append(stack.pop()[1])
            if stack and stack[-1][0] == "list":
                result.append("<li>")
                stack.append((tag, "</li>"))
            else:
                result.append(html.escape(match.group()))
        else:
            output_tag = "ul" if tag == "list" else tag
            result.append(f"<{output_tag}>")
            stack.append((tag, f"</{output_tag}>"))
    result.append(html.escape(text[position:]).replace("\n", "<br>"))
    result.extend(close for _, close in reversed(stack))
    return "".join(result)


def item_row(
    item: CatalogMod | CatalogCollection,
    install_state: Callable[[CatalogMod], InstallState],
    game_version: str,
) -> dict[str, Any]:
    incompatible = bool(
        game_version
        and item.supported_versions
        and game_version not in item.supported_versions
    )
    file_size, votes, member_count = "Unknown", "Unrated", 0
    if isinstance(item, CatalogMod):
        state = install_state(item)
        incompatible = incompatible or item.incompatible
        source_label = "Steam Workshop"
        action_label = {
            "missing": "Download",
            "installed": "Installed",
            "outdated": "Update",
        }[state]
        if item.file_size is not None:
            file_size = f"{int(item.file_size) / 1048576:.1f} MB"
        if item.votes is not None:
            votes = f"{item.votes.up} up / {item.votes.down} down"
    else:
        state = "missing"
        source_label = (
            "PxModRim pick" if item.source == "picked" else "Steam collection"
        )
        action_label = "Download all"
        member_count = item.member_count
    versions = set(item.supported_versions)
    return {
        "itemId": item.id,
        "kind": item.kind,
        "title": item.title,
        "author": item.author.name or item.author.id or "Unknown author",
        "previewUrl": safe_url(
            item.preview_url
            or next((p.url for p in item.previews if p.type == "image"), "")
        ),
        "collage": [
            url
            for url in (
                safe_url(u)
                for u in (
                    item.member_previews if isinstance(item, CatalogCollection) else []
                )
            )
            if url
        ],
        "sourceLabel": source_label,
        "versions": ", ".join(
            sorted(versions, key=lambda version: tuple(map(int, version.split("."))))
        )
        or "Not specified",
        "tags": " · ".join(tag for tag in item.tags if tag not in versions),
        "state": state,
        "queued": False,
        "stateLabel": {
            "missing": "Not installed",
            "installed": "Installed",
            "outdated": "Update available",
        }[state],
        "actionLabel": action_label,
        "incompatible": incompatible,
        "memberCount": member_count,
        "description": description_rich_text(item.description, item.description_format),
        "workshopUrl": safe_url(item.workshop_url or ""),
        "fileSize": file_size,
        "votes": votes,
    }


class CatalogListModel(QAbstractListModel):
    changed = Signal()
    _roles = (
        "itemId",
        "kind",
        "title",
        "author",
        "previewUrl",
        "collage",
        "sourceLabel",
        "versions",
        "tags",
        "state",
        "stateLabel",
        "actionLabel",
        "incompatible",
        "memberCount",
        "description",
        "workshopUrl",
        "fileSize",
        "votes",
        "queued",
    )

    def __init__(self, parent: QObject) -> None:
        super().__init__(parent)
        self._rows: list[dict[str, Any]] = []
        self._cursor: str | None = None
        self._total = 0

    def set_page(
        self,
        rows: Sequence[dict[str, Any]],
        total: int,
        cursor: str | None,
        *,
        append: bool = False,
    ) -> None:
        if append:
            known = {row["itemId"] for row in self._rows}
            additions = []
            for row in rows:
                if row["itemId"] not in known:
                    additions.append(row)
                    known.add(row["itemId"])
            if additions:
                self.beginInsertRows(
                    QModelIndex(), len(self._rows), len(self._rows) + len(additions) - 1
                )
                self._rows.extend(additions)
                self.endInsertRows()
        else:
            self.beginResetModel()
            self._rows = list(rows)
            self.endResetModel()
        self._total, self._cursor = total, cursor
        self.changed.emit()

    def refresh_rows(self, convert: Callable[[dict[str, Any]], dict[str, Any]]) -> None:
        self._rows = [convert(row) for row in self._rows]
        if self._rows:
            self.dataChanged.emit(self.index(0), self.index(len(self._rows) - 1))

    def rowCount(
        self, parent: QModelIndex | QPersistentModelIndex | None = None
    ) -> int:
        return len(self._rows) if parent is None or not parent.isValid() else 0

    def data(
        self,
        index: QModelIndex | QPersistentModelIndex,
        role: int = Qt.ItemDataRole.DisplayRole,
    ) -> Any:
        offset = role - int(Qt.ItemDataRole.UserRole) - 1
        if (
            not index.isValid()
            or not 0 <= index.row() < len(self._rows)
            or not 0 <= offset < len(self._roles)
        ):
            return None
        return self._rows[index.row()][self._roles[offset]]

    def roleNames(self) -> dict[int, QByteArray]:
        return {
            int(Qt.ItemDataRole.UserRole) + offset + 1: QByteArray(name.encode())
            for offset, name in enumerate(self._roles)
        }

    @property
    def cursor(self) -> str | None:
        return self._cursor

    @Property(bool, notify=changed)  # type: ignore[arg-type]
    def hasMore(self) -> bool:
        return self._cursor is not None

    @Property(int, notify=changed)  # type: ignore[arg-type]
    def count(self) -> int:
        return len(self._rows)

    @Property(int, notify=changed)  # type: ignore[arg-type]
    def total(self) -> int:
        return self._total
