from __future__ import annotations

import msgspec


class CollectionStats(msgspec.Struct):
    total: int = 0
    active: int = 0
    inactive: int = 0
    errors: int = 0
