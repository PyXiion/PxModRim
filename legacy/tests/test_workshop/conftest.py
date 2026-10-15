from __future__ import annotations

from typing import Any

import pytest


@pytest.fixture
def mod_payload() -> dict[str, Any]:
    return {
        "id": "2009463077",
        "kind": "mod",
        "source": "steam",
        "title": "Harmony",
        "author": {
            "id": "76561198000000000",
            "name": "Author",
            "profile_url": "https://steamcommunity.com/profiles/76561198000000000",
        },
        "description": "[b]Untrusted[/b] <script>doNotExecute()</script>",
        "description_format": "bbcode",
        "preview_url": "https://images.example/harmony.jpg",
        "previews": [
            {"type": "image", "url": "https://images.example/harmony.jpg"},
            {"type": "video", "url": "https://www.youtube.com/watch?v=example"},
        ],
        "workshop_url": "https://steamcommunity.com/sharedfiles/filedetails/?id=2009463077",
        "tags": ["1.6", "Libraries"],
        "supported_versions": ["1.5", "1.6"],
        "created_at": 1600000000,
        "updated_at": 1700000000,
        "file_size": "18446744073709551615",
        "subscriptions": 12345,
        "votes": {"up": 9, "down": 1, "positive_percent": 90},
        "dependencies": ["818773962"],
        "package_id": "brrainz.harmony",
        "incompatible": False,
    }


@pytest.fixture
def collection_payload() -> dict[str, Any]:
    return {
        "id": "picked:starter",
        "kind": "collection",
        "source": "picked",
        "steam_id": None,
        "title": "Starter pack",
        "author": {"id": None, "name": "PxModRim", "profile_url": None},
        "description": "<b>Plain text</b>",
        "description_format": "text",
        "preview_url": None,
        "previews": [],
        "workshop_url": None,
        "tags": ["Quality of Life"],
        "supported_versions": ["1.6"],
        "created_at": 1600000000,
        "updated_at": 1700000000,
        "member_ids": ["2009463077"],
        "member_count": 1,
        "member_previews": [],
        "total_size": None,
        "no_common_version": False,
    }
