from __future__ import annotations

import contextlib
import functools
import os
from collections.abc import Iterable, Iterator, MutableSet, Set
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Literal
from uuid import uuid4

import msgspec

from pxmodrim.core.constants import RIMWORLD_DLC_METADATA


@dataclass(slots=True)
class ReplacementInfo:
    """Replacement info for a mod from the database or local data."""

    name: str
    author: str
    packageid: str
    pfid: str
    supportedversions: list[str]
    source: str = "database"


class CaseInsensitiveStr(str):
    """Case-insensitive string that stores the lowered version."""

    def __new__(cls, pid: str) -> CaseInsensitiveStr:
        return super().__new__(cls, pid.lower())


class CaseInsensitiveSet(MutableSet[CaseInsensitiveStr]):
    """Mutable set with case-insensitive string membership."""

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
    """Parsed ModsConfig.xml data with active mods and known expansions."""

    __slots__ = ("version", "_activeMods", "_knownExpansions")

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
        self._activeMods.clear()

    def clear_all(self) -> None:
        self._activeMods.clear()
        self._knownExpansions.clear()

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
        """True if the activeMods list contains duplicate entries."""
        return len(self.activeMods) != len(set(self.activeMods))

    def check_expansions_duplicates(self) -> bool:
        """True if the knownExpansions list contains duplicate entries."""
        return len(self.knownExpansions) != len(set(self.knownExpansions))

    def to_dict(self) -> dict[str, Any]:
        """Serialize ModsConfig to a plain dictionary matching XML li structure."""
        return {
            "version": self.version,
            "activeMods": {"li": [str(i) for i in self.activeMods]},
            "knownExpansions": {"li": [str(i) for i in self.knownExpansions]},
        }


@dataclass
class BaseMod:
    """Base dataclass for all mod types with a name and UUID."""

    name: str = "Unknown Mod Name"
    _uuid: str = str(uuid4())

    @property
    def uuid(self) -> str:
        return self._uuid

    def __hash__(self) -> int:
        return hash(self.uuid)


@dataclass
class PackageIdMod:
    """Mixin dataclass adding a case-insensitive package ID."""

    package_id: CaseInsensitiveStr = CaseInsensitiveStr("invalid.mod")


@dataclass(slots=True)
class DependencyMod(BaseMod, PackageIdMod):
    """A mod dependency with workshop URL and alternative package IDs."""

    workshop_url: str = ""
    alternative_package_ids: set[CaseInsensitiveStr] = field(default_factory=set)


@dataclass(slots=True)
class ListedMod(BaseMod):
    """A mod discovered from any provider with validation and path info."""

    valid: bool = True
    supported_versions: set[str] = field(default_factory=set)
    description: str = (
        "This mod is considered invalid by PxModRim (and the RimWorld game)."
        + "\n\nThis mod does NOT contain an ./About/About.xml and is likely "
        "leftover from previous usage."
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
        """The folder name (stem) of the mod path, or None."""
        if self.mod_path:
            return self.mod_path.stem
        return None

    @property
    def internal_time_touched(self) -> int:
        """Modification timestamp of the mod folder, or -1 if unavailable."""
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
        """Published file ID from About/PublishedFileId.txt or numeric folder name."""
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
        """Whether the mod contains a non-empty Assemblies directory."""
        if self.mod_path is None:
            return False
        assemblies = self.mod_path / "Assemblies"
        return assemblies.is_dir() and any(True for _ in assemblies.iterdir())

    @functools.cached_property
    def xml_patch_mod(self) -> bool:
        """Whether the mod contains XML patch files in its Patches directory."""
        if self.mod_path is None:
            return False
        patches = self.mod_path / "Patches"
        if not patches.is_dir():
            return False
        return any(p.suffix == ".xml" for p in patches.iterdir())

    @property
    def preview_img_path(self) -> Path | None:
        """Path to About/Preview.png if it exists, otherwise None."""
        if self.mod_path is None:
            return None
        candidate = self.mod_path / "About/Preview.png"
        if candidate.exists():
            return candidate
        return None


@dataclass(slots=True)
class ScenarioMod(ListedMod):
    """A scenario mod with an optional summary."""

    summary: str = ""


@dataclass(slots=True)
class BaseRules:
    """Load order rules: load_after, load_before, incompatible_with, dependencies."""

    load_after: CaseInsensitiveSet = field(default_factory=CaseInsensitiveSet)
    load_before: CaseInsensitiveSet = field(default_factory=CaseInsensitiveSet)
    incompatible_with: CaseInsensitiveSet = field(default_factory=CaseInsensitiveSet)
    dependencies: dict[CaseInsensitiveStr, DependencyMod] = field(default_factory=dict)


@dataclass(slots=True)
class Rules(BaseRules):
    """Load order rules extended with load_first and load_last flags."""

    load_first: bool = False
    load_last: bool = False


@dataclass(slots=True)
class AboutXmlMod(ListedMod, PackageIdMod):
    """A mod with an About.xml, including authors, version, rules, and DLC metadata."""

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
        """Merged rules from about.xml, community, user; prefers community overrides."""
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
        """Invalidate the cached `overall_rules` property."""
        with contextlib.suppress(AttributeError):
            del self.overall_rules

    def get_dlc_name(self) -> str | None:
        """Return the DLC name if this mod matches a known RimWorld DLC."""
        for _appid, meta in RIMWORLD_DLC_METADATA.items():
            if meta["packageid"] == str(self.package_id):
                return meta["name"]
        return None


@dataclass(slots=True)
class CompiledDependencyData:
    """Pre-computed dependency graphs and tier classification for sorting."""

    deps_graph: dict[str, set[str]] = field(default_factory=dict)
    rev_deps_graph: dict[str, set[str]] = field(default_factory=dict)
    tier_zero_mods: set[str] = field(default_factory=set)
    tier_one_mods: set[str] = field(default_factory=set)
    tier_three_mods: set[str] = field(default_factory=set)
    incompatibilities: dict[str, set[str]] = field(default_factory=dict)
    declared_incompatibilities: dict[str, set[str]] = field(default_factory=dict)


class SubExternalRule(msgspec.Struct, omit_defaults=True):
    """A single external rule entry with name and comment fields."""

    name: list[str] | str = msgspec.field(default_factory=str)
    comment: list[str] | str = msgspec.field(default_factory=str)


class SubExternalBoolRule(msgspec.Struct, omit_defaults=True):
    """An external boolean rule with an optional comment."""

    value: bool = False
    comment: list[str] | str = msgspec.field(default_factory=str)


class ExternalRule(msgspec.Struct, omit_defaults=True):
    """Community-sourced load order rule for a single package ID."""

    loadAfter: dict[str, SubExternalRule] = {}
    loadBefore: dict[str, SubExternalRule] = {}
    loadTop: SubExternalBoolRule = msgspec.field(default_factory=SubExternalBoolRule)
    loadBottom: SubExternalBoolRule = msgspec.field(default_factory=SubExternalBoolRule)


class ExternalRulesSchema(msgspec.Struct, omit_defaults=True):
    """Top-level schema for community rules, keyed by package ID."""

    timestamp: int = 0
    rules: dict[str, ExternalRule] = msgspec.field(default_factory=dict)


class SteamDbEntryDependency(msgspec.Struct, omit_defaults=True):
    """A dependency reference from the Steam database."""

    name: str = msgspec.field(default_factory=str)
    url: str = msgspec.field(default_factory=str)


class SteamDbEntryBlacklist(msgspec.Struct, omit_defaults=True):
    """Blacklist status with reason comment from the Steam database."""

    value: bool = False
    comment: str = msgspec.field(default_factory=str)


class SteamDbEntry(msgspec.Struct, omit_defaults=True):
    """A single entry in the Steam Workshop database."""

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
    """Top-level Steam Workshop database schema with version and entries."""

    version: int = 0
    database: dict[str, SteamDbEntry] = msgspec.field(default_factory=dict)


ModMetadata = dict[str, Any]


@dataclass(slots=True)
class WorkshopUpdateResult:
    """Result of a workshop mod update operation."""

    status: Literal["success", "no_workshop_mods", "partial", "failed"]
    mods_checked: int
    mods_updated: int
    failed_pfids: list[str]
    errors: list[str]
