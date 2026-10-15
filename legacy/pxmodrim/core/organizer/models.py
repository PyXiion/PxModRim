from __future__ import annotations

from typing import Literal

import msgspec

ROOT_ID = 1
MAX_DEPTH = 3

RuleField = Literal["package_id", "name", "author"]
RuleOp = Literal["prefix", "contains", "equals"]


class OrganizerError(ValueError):
    """Raised when an organizer operation violates domain constraints."""


class Folder(msgspec.Struct, frozen=True):
    id: int
    parent_id: int | None
    name: str
    collapsed: bool = False


class Tag(msgspec.Struct, frozen=True):
    id: int
    name: str
    color: str


class RuleSpec(msgspec.Struct, frozen=True):
    field: RuleField
    op: RuleOp
    pattern: str
    folder_id: int


class Rule(msgspec.Struct, frozen=True):
    id: int
    position: int
    field: RuleField
    op: RuleOp
    pattern: str
    folder_id: int


class OrganizerState(msgspec.Struct, frozen=True):
    folders: dict[int, Folder]
    placements: dict[str, int]
    tags: dict[int, Tag]
    mod_tags: dict[str, frozenset[int]]
    rules: tuple[Rule, ...]
