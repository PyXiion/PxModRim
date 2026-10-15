from __future__ import annotations

from pathlib import Path


class CommunityRulesMissingError(Exception):
    def __init__(self, path: Path):
        self.path = path
        super().__init__(f"Community rules file not found: {path}")
