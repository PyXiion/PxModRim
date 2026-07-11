from __future__ import annotations

import functools
import os
from collections.abc import Iterable, Iterator, MutableSet, Set
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Literal
from uuid import uuid4

import msgspec

from pxmodrim._compat.constants import RIMWORLD_DLC_METADATA


@dataclass
class ReplacementInfo:
    name: str
    author: str
    packageid: str
    pfid: str
    supportedversions: list[str]
    source: str = "database"


class CaseInsensitiveStr(str):
    def __new__(cls, pid: str) -> CaseInsensitiveStr:
        return super().__new__(cls, pid.lower())


class CaseInsensitiveSet(MutableSet[CaseInsensitiveStr]):
    def __init__(
        self,
        s: Iterable[CaseInsensitiveStr | str] | CaseInsensitiveStr | str | None = (),
    ):
        if isinstance(s, str):
            data = {CaseInsensitiveStr(s)}
        elif isinstance(s, Iterable):
            data = {CaseInsensitiveStr(i) for i in s}
        elif not s:
            data = set()
        else:
            raise TypeError(f"Unsupported type, got {type(s)}")
        self._data: set[CaseInsensitiveStr] = data

    def __contains__(self, value: Any) -> bool:
        if not isinstance(value, CaseInsensitiveStr) and isinstance(value, str):
            value = CaseInsensitiveStr(value)
        elif not isinstance(value, CaseInsensitiveStr):
            return False
        return value in self._data

    def __iter__(self) -> Iterator[CaseInsensitiveStr]:
        return iter(self._data)

    def __len__(self) -> int:
        return len(self._data)

    def __or__(self, other: Set[Any]) -> CaseInsensitiveSet:
        return CaseInsensitiveSet(self._data | {CaseInsensitiveStr(i) for i in other})

    def __ror__(self, other: Set[Any]) -> CaseInsensitiveSet:
        return self.__or__(other)

    def __hash__(self) -> int:
        return hash(self._data)

    def __eq__(self, other: Any) -> bool:
        if isinstance(other, CaseInsensitiveSet):
            return self._data == other._data
        if isinstance(other, Set):
            return self._data == {CaseInsensitiveStr(i) for i in other}
        return False

    def discard(self, value: CaseInsensitiveStr | str) -> None:
        if not isinstance(value, CaseInsensitiveStr) and isinstance(value, str):
            value = CaseInsensitiveStr(value)
        return self._data.discard(value)

    def add(self, value: CaseInsensitiveStr | str) -> None:
        if not isinstance(value, CaseInsensitiveStr) and isinstance(value, str):
            value = CaseInsensitiveStr(value)
        return self._data.add(value)

    def __and__(self, other: Set[Any]) -> Set[CaseInsensitiveStr]:
        return super().__and__(other)


class ModsConfig:
    version: str
    _activeMods: list[CaseInsensitiveStr]
    _knownExpansions: list[CaseInsensitiveStr]

    def __init__(
        self,
        version: str,
        activeMods: list[CaseInsensitiveStr],
        knownExpansions: list[CaseInsensitiveStr],
    ):
        self.version = version
        self.activeMods = activeMods
        self.knownExpansions = knownExpansions

    def clear_active_mods(self) -> None:
        self.activeMods.clear()

    def clear_all(self) -> None:
        self.clear_active_mods()
        self.knownExpansions.clear()

    @property
    def activeMods(self) -> list[CaseInsensitiveStr]:
        return self._activeMods[:]

    @activeMods.setter
    def activeMods(self, value: list[CaseInsensitiveStr] | list[str]) -> None:
        if isinstance(value, list):
            self._activeMods = [CaseInsensitiveStr(i) for i in value]
        else:
            raise TypeError(f"Expected list, got {type(value)}")

    @property
    def knownExpansions(self) -> list[CaseInsensitiveStr]:
        return self._knownExpansions[:]

    @knownExpansions.setter
    def knownExpansions(self, value: list[CaseInsensitiveStr] | list[str]) -> None:
        if isinstance(value, list):
            self._knownExpansions = [CaseInsensitiveStr(i) for i in value]
        else:
            raise TypeError(f"Expected list, got {type(value)}")

    def check_active_duplicates(self) -> bool:
        return len(self.activeMods) != len(set(self.activeMods))

    def check_expansions_duplicates(self) -> bool:
        return len(self.knownExpansions) != len(set(self.knownExpansions))

    def to_dict(self) -> dict[str, Any]:
        return {
            "version": self.version,
            "activeMods": [str(i) for i in self.activeMods],
            "knownExpansions": [str(i) for i in self.knownExpansions],
        }


@dataclass
class BaseMod:
    name: str = "Unknown Mod Name"
    _uuid: str = str(uuid4())

    @property
    def uuid(self) -> str:
        return self._uuid

    def __hash__(self) -> int:
        return hash(self.uuid)


@dataclass
class PackageIdMod:
    package_id: CaseInsensitiveStr = CaseInsensitiveStr("invalid.mod")


@dataclass
class DependencyMod(BaseMod, PackageIdMod):
    workshop_url: str = ""
    alternative_package_ids: set[CaseInsensitiveStr] = field(default_factory=set)


@dataclass
class ListedMod(BaseMod):
    valid: bool = True
    supported_versions: set[str] = field(default_factory=set)
    description: str = (
        "This mod is considered invalid by PxModRim (and the RimWorld game)."
        + "\n\nThis mod does NOT contain an ./About/About.xml and is likely leftover from previous usage."
    )

    _mod_path: Path | None = None
    provider_id: str = ""
    obsolete: bool = False
    db_builder_no_name: bool = False

    @property
    def mod_path(self) -> Path | None:
        return self._mod_path

    @mod_path.setter
    def mod_path(self, path: Path) -> None:
        if self._mod_path:
            raise ValueError("Mod path already set. Cannot override.")
        self._mod_path = path
        if hasattr(self, "published_file_id"):
            del self.published_file_id

    @property
    def mod_folder(self) -> str | None:
        if self.mod_path:
            return self.mod_path.stem
        return None

    @property
    def internal_time_touched(self) -> int:
        if self.mod_path and os.path.exists(self.mod_path):
            return int(os.path.getmtime(self.mod_path))
        return -1

    @property
    def uuid(self) -> str:
        if self._mod_path:
            return str(self._mod_path)
        return self._uuid

    @functools.cached_property
    def published_file_id(self) -> str | None:
        if self.mod_path is None:
            return None
        pfid_path = self.mod_path / "About/PublishedFileId.txt"
        if pfid_path.exists():
            try:
                content = pfid_path.read_text(encoding="utf-8-sig").strip()
            except OSError:
                return None
            if content.isdigit() and content:
                return content
        if self.mod_folder is not None and self.mod_folder.isdigit():
            return self.mod_folder
        return None

    @functools.cached_property
    def c_sharp_mod(self) -> bool:
        if self.mod_path is None:
            return False
        assemblies = self.mod_path / "Assemblies"
        return assemblies.is_dir() and any(True for _ in assemblies.iterdir())

    @functools.cached_property
    def xml_patch_mod(self) -> bool:
        if self.mod_path is None:
            return False
        patches = self.mod_path / "Patches"
        if not patches.is_dir():
            return False
        return any(p.suffix == ".xml" for p in patches.iterdir())

    @property
    def preview_img_path(self) -> Path | None:
        if self.mod_path is None:
            return None
        candidate = self.mod_path / "About/Preview.png"
        if candidate.exists():
            return candidate
        return None


@dataclass
class ScenarioMod(ListedMod):
    summary: str = ""


@dataclass
class BaseRules:
    load_after: CaseInsensitiveSet = field(default_factory=CaseInsensitiveSet)
    load_before: CaseInsensitiveSet = field(default_factory=CaseInsensitiveSet)
    incompatible_with: CaseInsensitiveSet = field(default_factory=CaseInsensitiveSet)
    dependencies: dict[CaseInsensitiveStr, DependencyMod] = field(default_factory=dict)


@dataclass
class Rules(BaseRules):
    load_first: bool = False
    load_last: bool = False


@dataclass
class AboutXmlMod(ListedMod, PackageIdMod):
    authors: list[str] = field(default_factory=list)
    mod_version: str = ""
    mod_icon_path: Path | None = None
    steam_app_id: int = -1
    url: str = ""

    about_rules: BaseRules = field(default_factory=BaseRules)
    community_rules: Rules = field(default_factory=Rules)
    user_rules: Rules = field(default_factory=Rules)

    @functools.cached_property
    def overall_rules(self) -> Rules:
        overall = Rules()

        overall.load_before = (
            self.about_rules.load_before
            | self.community_rules.load_before
            | self.user_rules.load_before
        )
        overall.load_after = (
            self.about_rules.load_after
            | self.community_rules.load_after
            | self.user_rules.load_after
        )
        overall.incompatible_with = (
            self.about_rules.incompatible_with
            | self.community_rules.incompatible_with
            | self.user_rules.incompatible_with
        )
        overall.dependencies = {
            **self.about_rules.dependencies,
            **self.community_rules.dependencies,
            **self.user_rules.dependencies,
        }
        overall.load_first = (
            self.community_rules.load_first or self.user_rules.load_first
        )
        overall.load_last = self.community_rules.load_last or self.user_rules.load_last

        return overall

    def clear_cache(self) -> None:
        try:
            del self.overall_rules
        except AttributeError:
            pass

    def get_dlc_name(self) -> str | None:
        for appid, meta in RIMWORLD_DLC_METADATA.items():
            if meta["packageid"] == str(self.package_id):
                return meta["name"]
        return None


@dataclass
class CompiledDependencyData:
    deps_graph: dict[str, set[str]] = field(default_factory=dict)
    rev_deps_graph: dict[str, set[str]] = field(default_factory=dict)
    tier_zero_mods: set[str] = field(default_factory=set)
    tier_one_mods: set[str] = field(default_factory=set)
    tier_three_mods: set[str] = field(default_factory=set)
    incompatibilities: dict[str, set[str]] = field(default_factory=dict)
    declared_incompatibilities: dict[str, set[str]] = field(default_factory=dict)


class SubExternalRule(msgspec.Struct, omit_defaults=True):
    name: list[str] | str = msgspec.field(default_factory=str)
    comment: list[str] | str = msgspec.field(default_factory=str)


class SubExternalBoolRule(msgspec.Struct, omit_defaults=True):
    value: bool = False
    comment: list[str] | str = msgspec.field(default_factory=str)


class ExternalRule(msgspec.Struct, omit_defaults=True):
    loadAfter: dict[str, SubExternalRule] = {}
    loadBefore: dict[str, SubExternalRule] = {}
    loadTop: SubExternalBoolRule = msgspec.field(default_factory=SubExternalBoolRule)
    loadBottom: SubExternalBoolRule = msgspec.field(default_factory=SubExternalBoolRule)


class ExternalRulesSchema(msgspec.Struct, omit_defaults=True):
    timestamp: int = 0
    rules: dict[str, ExternalRule] = msgspec.field(default_factory=dict)


class SteamDbEntryDependency(msgspec.Struct, omit_defaults=True):
    name: str = msgspec.field(default_factory=str)
    url: str = msgspec.field(default_factory=str)


class SteamDbEntryBlacklist(msgspec.Struct, omit_defaults=True):
    value: bool = False
    comment: str = msgspec.field(default_factory=str)


class SteamDbEntry(msgspec.Struct, omit_defaults=True):
    unpublished: bool = False
    url: str = msgspec.field(default_factory=str)
    packageId: str = msgspec.field(default_factory=str)
    gameVersions: list[str | None] | str | None = msgspec.field(default_factory=list)
    steamName: str = msgspec.field(default_factory=str)
    name: str = msgspec.field(default_factory=str)
    authors: list[str] | str | None = msgspec.field(default_factory=str)
    dependencies: dict[str, list[str] | SteamDbEntryDependency] = msgspec.field(
        default_factory=dict
    )
    blacklist: SteamDbEntryBlacklist = msgspec.field(
        default_factory=SteamDbEntryBlacklist
    )
    tags: list[dict[str, str]] = msgspec.field(default_factory=list)


class SteamDbSchema(msgspec.Struct):
    version: int = 0
    database: dict[str, SteamDbEntry] = msgspec.field(default_factory=dict)


ModMetadata = dict[str, Any]


@dataclass
class WorkshopUpdateResult:
    status: Literal["success", "no_workshop_mods", "partial", "failed"]
    mods_checked: int
    mods_updated: int
    failed_pfids: list[str]
    errors: list[str]
