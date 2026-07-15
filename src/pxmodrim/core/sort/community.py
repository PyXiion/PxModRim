from __future__ import annotations

import os
from pathlib import Path
from typing import Any

import msgspec

from pxmodrim.core.models.metadata.structures import BaseRules
from pxmodrim.core.msgspec_hooks import dec_hook
from pxmodrim.core.sort.models import CommunityRule, PackageId

COMMUNITY_RULES_URL = (
    "https://github.com/RimSort/Community-Rules-Database/archive/refs/heads/main.zip"
)
COMMUNITY_RULES_FILENAME = "communityRules.json"


def config_dir() -> Path:
    base = os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")
    return Path(base) / "pxmodrim"


def community_rules_path() -> Path:
    return config_dir() / COMMUNITY_RULES_FILENAME


class ExternalRule(msgspec.Struct, omit_defaults=True):
    """A single community rule specifying load-ordering relationships for a mod."""

    loadAfter: dict[str, Any] = {}
    loadBefore: dict[str, Any] = {}
    loadTop: SubExternalBoolRule = msgspec.field(
        default_factory=lambda: SubExternalBoolRule()
    )
    loadBottom: SubExternalBoolRule = msgspec.field(
        default_factory=lambda: SubExternalBoolRule()
    )


class SubExternalBoolRule(msgspec.Struct, omit_defaults=True):
    value: bool = False


class ExternalRulesSchema(msgspec.Struct, omit_defaults=True):
    """Schema for the community rules JSON file, wrapping a timestamp and rule map."""

    timestamp: int = 0
    rules: dict[str, ExternalRule] = {}

    def to_community_rules(self) -> dict[PackageId, CommunityRule]:
        result: dict[PackageId, CommunityRule] = {}
        for pid_str, rule in self.rules.items():
            pid = PackageId(pid_str)
            result[pid] = CommunityRule(
                package_id=pid,
                load_after={PackageId(x) for x in rule.loadAfter},
                load_before={PackageId(x) for x in rule.loadBefore},
                load_first=rule.loadTop.value,
                load_last=rule.loadBottom.value,
                incompatible_with=set(),
            )
        return result


def load_community_rules(json_path: Path) -> dict[PackageId, CommunityRule]:
    """Load and deserialize community rules from a JSON file on disk."""
    if not json_path.exists():
        return {}
    data = msgspec.json.decode(
        json_path.read_bytes(), type=ExternalRulesSchema, dec_hook=dec_hook
    )
    return data.to_community_rules()


def merge_community_rules(
    mod_rules: BaseRules,
    community_rules: dict[PackageId, CommunityRule],
    mod_pid: PackageId,
) -> BaseRules:
    """Merge community-sourced ordering rules into a mod's own BaseRules."""
    if mod_pid not in community_rules:
        return mod_rules

    cr = community_rules[mod_pid]
    return BaseRules(
        load_after=mod_rules.load_after | cr.load_after,
        load_before=mod_rules.load_before | cr.load_before,
        incompatible_with=mod_rules.incompatible_with,
        dependencies=dict(mod_rules.dependencies),
    )
