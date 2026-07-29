from __future__ import annotations

import re
import traceback
from collections.abc import Sequence
from pathlib import Path
from typing import Any

import lxml.etree as ET
from loguru import logger

from pxmodrim.core.constants import (
    DEFAULT_MISSING_PACKAGEID,
    RIMWORLD_DLC_METADATA,
    RIMWORLD_STEAM_APP_ID,
)
from pxmodrim.core.models.metadata.structures import (
    AboutXmlMod,
    BaseRules,
    CaseInsensitiveSet,
    CaseInsensitiveStr,
    DependencyMod,
    ListedMod,
)
from pxmodrim.core.utils import find_about_xml
from pxmodrim.core.xml import _text


class MalformedDataException(Exception):
    """Raised when parsed About.xml data is structurally invalid."""

    def __init__(self, message: str):
        super().__init__(message)
        self.message = f"Malformed data: {message}"


def value_extractor(
    input: dict[str, str] | dict[str, list[str]] | Sequence[str] | str | None,
    strip_str: bool = True,
) -> str | list[Any] | dict[str, str] | dict[str, list[str]] | None:
    """Normalize XML-parsed values, unwrap single-entry dicts and strip strings."""
    if input is None:
        return None
    if isinstance(input, str):
        return input.strip() if strip_str else input
    if isinstance(input, Sequence):
        return [value_extractor(item) for item in input]
    if isinstance(input, dict):
        if len(input) == 1:
            return value_extractor(next(iter(input.values())))
        if input.keys() == {"@IgnoreIfNoMatchingField", "#text"}:
            return input["#text"]
        return input
    return input


def match_version(
    input: dict[str, str] | dict[str, list[str]],
    target_version: str,
    stop_at_first: bool = True,
) -> tuple[bool, None | list[str] | str]:
    """Match a versioned dict entry by major.minor, return value(s) on success."""
    try:
        major, minor = target_version.split(".")[:2]
        version_regex = f"v{major}.{minor}"
    except ValueError:
        return False, None

    if stop_at_first:
        result = input.get(version_regex) or input.get(f"{major}.{minor}")
        if result is not None:
            return True, result

    results = []
    for key, value in input.items():
        if re.match(version_regex, key):
            if stop_at_first:
                return True, value
            if isinstance(value, list):
                results.extend(value)
            else:
                results.append(value)

    if not results:
        return False, None

    return True, results


def _set_mod_invalid(mod: ListedMod, message: str) -> ListedMod:
    mod.valid = False
    logger.warning(message)
    return mod


def _parse_basic(mod_data: dict[str, Any], mod: AboutXmlMod) -> AboutXmlMod:
    """Populate an AboutXmlMod with basic fields (packageId, name, authors)."""
    package_id = value_extractor(mod_data.get("packageId", False))
    if isinstance(package_id, str) and package_id.strip():
        mod.package_id = CaseInsensitiveStr(package_id)
    else:
        mod.package_id = CaseInsensitiveStr(DEFAULT_MISSING_PACKAGEID)
        logger.warning(
            f"packageId missing or invalid: {package_id}. "
            f"Assigned sentinel '{DEFAULT_MISSING_PACKAGEID}'."
        )

    steam_app_id = value_extractor(mod_data.get("steamAppId", False))
    if isinstance(steam_app_id, str) and steam_app_id.isdigit():
        mod.steam_app_id = int(steam_app_id)
    elif str(mod.package_id) in _get_dlc_packageid_map():
        mod.steam_app_id = int(_get_dlc_packageid_map()[str(mod.package_id)])

    dlc_appid = _get_dlc_packageid_map().get(str(mod.package_id))
    dlc_meta = RIMWORLD_DLC_METADATA.get(dlc_appid, {}) if dlc_appid else {}

    name = value_extractor(mod_data.get("name", False))
    if isinstance(name, str):
        mod.name = name
    elif dlc_meta:
        mod.name = dlc_meta["name"]
    else:
        mod.name = str(mod.package_id)

    description = value_extractor(mod_data.get("description", False))
    if isinstance(description, str):
        mod.description = description
    elif dlc_meta:
        mod.description = dlc_meta["description"]

    author = value_extractor(mod_data.get("author", False))
    authors = value_extractor(mod_data.get("authors", False))

    if isinstance(author, str):
        mod.authors.append(author)

    normalized_authors: list[str] = []
    if isinstance(authors, dict) and authors.get("li"):
        li = authors.get("li")
        if isinstance(li, list):
            normalized_authors = [str(a) for a in li if a]
        elif isinstance(li, str):
            normalized_authors = [li]
    elif isinstance(authors, list):
        normalized_authors = [str(a) for a in authors if a]
    elif isinstance(authors, str):
        normalized_authors = [authors]
    else:
        normalized_authors = []

    mod.authors.extend(normalized_authors)

    supported_versions = value_extractor(mod_data.get("supportedVersions", False))
    if isinstance(supported_versions, list):
        mod.supported_versions = set(supported_versions)
    elif isinstance(supported_versions, str):
        mod.supported_versions = {supported_versions}

    return mod


def _parse_optional(
    mod_data: dict[str, Any],
    mod: AboutXmlMod,
    target_version: str,
    prefer_versioned: bool = True,
) -> AboutXmlMod:
    """Populate optional AboutXmlMod fields (modVersion, icon, url, rules)."""
    mod_version = value_extractor(mod_data.get("modVersion", False))
    if mod_version and isinstance(mod_version, str):
        mod.mod_version = mod_version

    mod_icon_path = value_extractor(mod_data.get("modIconPath", False))
    if mod_icon_path and isinstance(mod_icon_path, str):
        mod.mod_icon_path = Path(mod_icon_path)

    url = value_extractor(mod_data.get("url", False))
    if url and isinstance(url, str):
        mod.url = url

    mod.about_rules = create_base_rules(mod_data, target_version, prefer_versioned)

    dlc_map = _get_dlc_packageid_map()
    str_pid = str(mod.package_id)
    if str_pid in dlc_map and dlc_map[str_pid] != RIMWORLD_STEAM_APP_ID:
        rimworld_pid = CaseInsensitiveStr("ludeon.rimworld")
        if rimworld_pid not in mod.about_rules.dependencies:
            mod.about_rules.dependencies[rimworld_pid] = DependencyMod(
                package_id=rimworld_pid,
                name="RimWorld",
                workshop_url="https://store.steampowered.com/app/294100/RimWorld",
            )

    descriptions_by_version: bool | dict[str, str] = mod_data.get(
        "descriptionsByVersion", False
    )
    if isinstance(descriptions_by_version, dict):
        _, description = match_version(descriptions_by_version, target_version)
        if description and isinstance(description, str):
            mod.description = description

    return mod


def _match_byversion_raw(
    byversion_data: dict[str, Any],
    target_version: str,
) -> tuple[bool, Any]:
    """Match versioned raw data by major.minor key, returning the first match."""
    try:
        major, minor = target_version.split(".")[:2]
    except ValueError:
        return False, None

    version_regex = f"v{major}.{minor}"

    if version_regex in byversion_data:
        return True, byversion_data[version_regex]
    if f"{major}.{minor}" in byversion_data:
        return True, byversion_data[f"{major}.{minor}"]

    for key, value in byversion_data.items():
        if re.match(version_regex, key):
            return True, value

    return False, None


def create_mod_dependency(input_dict: dict[str, str]) -> DependencyMod:
    """Build a DependencyMod from a parsed dependency dictionary."""
    mod = DependencyMod()
    package_id = input_dict.get("packageId", False)
    if isinstance(package_id, str):
        mod.package_id = CaseInsensitiveStr(package_id)

    name = input_dict.get("displayName", False)
    if isinstance(name, str):
        mod.name = name

    workshop_url = input_dict.get("workshopUrl", False)
    if isinstance(workshop_url, str):
        mod.workshop_url = workshop_url

    alts = input_dict.get("alternativePackageIds", False)
    if isinstance(alts, list):
        for a in alts:
            if isinstance(a, str) and a.strip():
                mod.alternative_package_ids.add(CaseInsensitiveStr(a))

    return mod


def create_base_rules(
    mod_data: dict[str, Any],
    target_version: str,
    prefer_versioned: bool = True,
) -> BaseRules:
    """Build BaseRules from parsed mod_data, resolving versioned deps and load-order."""
    rules = BaseRules()

    mod_dependencies = value_extractor(mod_data.get("modDependencies", []))
    if mod_dependencies is None:
        mod_dependencies = []
    mod_dependencies = (
        mod_dependencies if isinstance(mod_dependencies, list) else [mod_dependencies]
    )

    if prefer_versioned:
        byversion_deps = mod_data.get("modDependenciesByVersion", {})
        if isinstance(byversion_deps, dict) and byversion_deps:
            matched, versioned_deps_raw = _match_byversion_raw(
                byversion_deps, target_version
            )
            if matched:
                versioned_deps = (
                    value_extractor(versioned_deps_raw) if versioned_deps_raw else []
                )
                mod_dependencies = (
                    versioned_deps
                    if isinstance(versioned_deps, list)
                    else [versioned_deps]
                    if versioned_deps
                    else []
                )

    for dependency in mod_dependencies:
        if isinstance(dependency, dict):
            if not dependency or dependency.get("@isNull") == "True":
                continue
            deps: dict[str, Any] = {}
            for key, value in dependency.items():
                if isinstance(value, str):
                    deps[key] = value
                elif key == "alternativePackageIds" and isinstance(value, dict):
                    alt_li = value.get("li")
                    alt_list: list[str] = []
                    if isinstance(alt_li, list):
                        for v in alt_li:
                            if isinstance(v, str):
                                alt_list.append(v)
                            elif (
                                isinstance(v, dict)
                                and "#text" in v
                                and isinstance(v["#text"], str)
                            ):
                                alt_list.append(v["#text"])
                    elif isinstance(alt_li, str):
                        alt_list.append(alt_li)
                    if alt_list:
                        deps["alternativePackageIds"] = alt_list
                elif isinstance(value, dict) and (
                    not value or value.get("@isNull") == "True"
                ):
                    continue
                else:
                    logger.warning(f"Skipping invalid dependency value: {value}.")

            dep = create_mod_dependency(deps)

            if dep.package_id in rules.dependencies:
                logger.warning(
                    f"Duplicate dependency found: {dep.package_id}. Skipping."
                )
            else:
                rules.dependencies[dep.package_id] = dep
        elif dependency:
            logger.warning(f"Skipping invalid dependency: {dependency}.")

    def load_operations(
        mod_data: dict[str, Any],
        key: str,
        force_key: str,
        target_version: str,
        prefer_versioned: bool,
    ) -> CaseInsensitiveSet:
        load = value_extractor(mod_data.get(key, []))
        if load is None:
            load = []
        load = load if isinstance(load, list) else [load]

        if prefer_versioned:
            byversion = mod_data.get(f"{key}ByVersion", {})
            if isinstance(byversion, dict) and byversion:
                matched, versioned_raw = _match_byversion_raw(byversion, target_version)
                if matched:
                    versioned = value_extractor(versioned_raw) if versioned_raw else []
                    load = (
                        versioned
                        if isinstance(versioned, list)
                        else [versioned]
                        if versioned
                        else []
                    )

        force_load = value_extractor(mod_data.get(force_key, []))
        if force_load is None:
            force_load = []
        force_load = force_load if isinstance(force_load, list) else [force_load]
        load.extend(force_load)

        load = [item for item in load if isinstance(item, (str, CaseInsensitiveStr))]

        return CaseInsensitiveSet(load)

    rules.load_before = load_operations(
        mod_data, "loadBefore", "forceLoadBefore", target_version, prefer_versioned
    )

    rules.load_after = load_operations(
        mod_data, "loadAfter", "forceLoadAfter", target_version, prefer_versioned
    )

    incompatible_with = value_extractor(mod_data.get("incompatibleWith", []))
    if incompatible_with is None:
        incompatible_with = []
    incompatible_with = (
        incompatible_with
        if isinstance(incompatible_with, list)
        else [incompatible_with]
    )

    if prefer_versioned:
        byversion_incompat = mod_data.get("incompatibleWithByVersion", {})
        if isinstance(byversion_incompat, dict) and byversion_incompat:
            matched, incompat_raw = _match_byversion_raw(
                byversion_incompat, target_version
            )
            if matched:
                versioned_incompat = (
                    value_extractor(incompat_raw) if incompat_raw else []
                )
                incompatible_with = (
                    versioned_incompat
                    if isinstance(versioned_incompat, list)
                    else [versioned_incompat]
                    if versioned_incompat
                    else []
                )

    incompatible_with = [
        item
        for item in incompatible_with
        if isinstance(item, (str, CaseInsensitiveStr))
    ]
    rules.incompatible_with = CaseInsensitiveSet(incompatible_with)

    return rules


def create_about_mod(
    mod_data: dict[str, Any],
    target_version: str,
    prefer_versioned: bool = True,
) -> tuple[bool, AboutXmlMod]:
    """Create an AboutXmlMod from parsed XML data. Returns (valid, mod)."""
    mod = _parse_basic(mod_data, AboutXmlMod())

    if not isinstance(mod, AboutXmlMod):
        ruled_mod = AboutXmlMod()
        ruled_mod.__dict__ = mod.__dict__
        mod = ruled_mod

    mod = _parse_optional(mod_data, mod, target_version, prefer_versioned)

    return mod.valid, mod


def _match_versioned_child(
    parent: ET._Element, target_version: str
) -> ET._Element | None:
    """Find the versioned child element matching target_version (*.*)."""
    try:
        major, minor = target_version.split(".")[:2]
    except ValueError:
        return None
    key = f"v{major}.{minor}"
    found = parent.find(key)
    if found is None:
        found = parent.find(f"{major}.{minor}")
    if found is not None:
        return found
    for child in parent:
        if re.match(key, child.tag):
            return child
    return None


def _dep_from_li(li: ET._Element) -> DependencyMod:
    """Parse a <li> element from <modDependencies> into a DependencyMod."""
    dep = DependencyMod()
    pid = _text(li, "packageId")
    if pid:
        dep.package_id = CaseInsensitiveStr(pid)
    name = _text(li, "displayName")
    if name:
        dep.name = name
    url = _text(li, "workshopUrl")
    if url:
        dep.workshop_url = url
    alt_el = li.find("alternativePackageIds")
    if alt_el is not None:
        for alt_li in alt_el:
            if alt_li.tag == "li" and alt_li.text and alt_li.text.strip():
                dep.alternative_package_ids.add(
                    CaseInsensitiveStr(alt_li.text.strip())
                )
    return dep


def _element_value(el: ET._Element | None) -> str | list[str] | None:
    """Extract text or <li> children as a string or list of strings."""
    if el is None:
        return None
    children = [ch for ch in el if ch.tag == "li"]
    if children:
        return [
            c.text.strip()
            for c in children
            if c.text and c.text.strip()
        ]
    if el.text:
        text = el.text.strip()
        return text or None
    return None


def _create_about_mod_from_element(
    root: ET._Element,
    target_version: str,
    prefer_versioned: bool = True,
) -> AboutXmlMod:
    """Build AboutXmlMod in a single pass over the <ModMetaData> element children."""
    mod = AboutXmlMod()
    rules = BaseRules()

    deps_el: ET._Element | None = None
    deps_bv: bool = False
    load_before_li: list[str] = []
    load_after_li: list[str] = []
    incompat_li: list[str] = []
    force_before_li: list[str] = []
    force_after_li: list[str] = []
    load_before_bv_el: ET._Element | None = None
    load_after_bv_el: ET._Element | None = None
    incompat_bv_el: ET._Element | None = None

    for child in root:
        tag = child.tag
        if tag == "packageId":
            t = _element_value(child)
            if isinstance(t, str) and t:
                mod.package_id = CaseInsensitiveStr(t)
            else:
                _set_mod_invalid(
                    mod,
                    f"packageId missing or invalid: {t}. "
                    f"Assigned sentinel '{DEFAULT_MISSING_PACKAGEID}'.",
                )
                mod.package_id = CaseInsensitiveStr(DEFAULT_MISSING_PACKAGEID)

        elif tag == "steamAppId":
            t = _element_value(child)
            if isinstance(t, str) and t.isdigit():
                mod.steam_app_id = int(t)

        elif tag == "name":
            t = _element_value(child)
            if isinstance(t, str):
                mod.name = t

        elif tag == "description":
            t = _element_value(child)
            if isinstance(t, str):
                mod.description = t

        elif tag == "author":
            t = _element_value(child)
            if isinstance(t, str):
                mod.authors.append(t)

        elif tag == "authors":
            t = _element_value(child)
            if isinstance(t, list):
                mod.authors.extend(t)

        elif tag == "supportedVersions":
            t = _element_value(child)
            if isinstance(t, list):
                mod.supported_versions = set(t)

        elif tag == "modVersion":
            t = _element_value(child)
            if isinstance(t, str):
                mod.mod_version = t

        elif tag == "modIconPath":
            t = _element_value(child)
            if isinstance(t, str):
                mod.mod_icon_path = Path(t)

        elif tag == "url":
            t = _element_value(child)
            if isinstance(t, str):
                mod.url = t

        elif tag == "modDependencies":
            if not deps_bv:
                deps_el = child
        elif tag == "modDependenciesByVersion":
            if prefer_versioned:
                matched = _match_versioned_child(child, target_version)
                if matched is not None:
                    deps_el = matched
                    deps_bv = True

        elif tag == "loadBefore":
            t = _element_value(child)
            if isinstance(t, list):
                load_before_li = t
        elif tag == "loadBeforeByVersion":
            if prefer_versioned:
                matched = _match_versioned_child(child, target_version)
                if matched is not None:
                    load_before_bv_el = matched

        elif tag == "loadAfter":
            t = _element_value(child)
            if isinstance(t, list):
                load_after_li = t
        elif tag == "loadAfterByVersion":
            if prefer_versioned:
                matched = _match_versioned_child(child, target_version)
                if matched is not None:
                    load_after_bv_el = matched

        elif tag == "forceLoadBefore":
            t = _element_value(child)
            if isinstance(t, list):
                force_before_li = t
        elif tag == "forceLoadAfter":
            t = _element_value(child)
            if isinstance(t, list):
                force_after_li = t

        elif tag == "incompatibleWith":
            t = _element_value(child)
            if isinstance(t, list):
                incompat_li = t
        elif tag == "incompatibleWithByVersion":
            if prefer_versioned:
                matched = _match_versioned_child(child, target_version)
                if matched is not None:
                    incompat_bv_el = matched

        elif tag == "descriptionsByVersion":
            matched = _match_versioned_child(child, target_version)
            if matched is not None and matched.text and matched.text.strip():
                mod.description = matched.text.strip()

    # Apply versioned overrides
    if load_before_bv_el is not None:
        t = _element_value(load_before_bv_el)
        if isinstance(t, list):
            load_before_li = t
    if load_after_bv_el is not None:
        t = _element_value(load_after_bv_el)
        if isinstance(t, list):
            load_after_li = t
    if incompat_bv_el is not None:
        t = _element_value(incompat_bv_el)
        if isinstance(t, list):
            incompat_li = t

    # Combine force + regular
    load_before_li.extend(force_before_li)
    load_after_li.extend(force_after_li)

    # Build rules
    rules.load_before = CaseInsensitiveSet(load_before_li)
    rules.load_after = CaseInsensitiveSet(load_after_li)
    rules.incompatible_with = CaseInsensitiveSet(incompat_li)

    # Process dependencies
    if deps_el is not None:
        for li in deps_el:
            if li.tag != "li":
                continue
            if li.attrib.get("isNull") == "True":
                continue
            if not li.attrib and len(li) == 0:
                continue
            dep = _dep_from_li(li)
            if dep.package_id in rules.dependencies:
                logger.warning(
                    f"Duplicate dependency found: {dep.package_id}. Skipping."
                )
            else:
                rules.dependencies[dep.package_id] = dep

    mod.about_rules = rules

    # DLC fallback for name/description/steamAppId
    str_pid = str(mod.package_id)
    dlc_appid = _get_dlc_packageid_map().get(str_pid)
    dlc_meta = RIMWORLD_DLC_METADATA.get(dlc_appid, {}) if dlc_appid else {}
    if dlc_meta:
        if not mod.name or mod.name == str_pid:
            mod.name = dlc_meta["name"]
        if not mod.description:
            mod.description = dlc_meta["description"]

    # DLC -> RimWorld dependency
    dlc_map = _get_dlc_packageid_map()
    if str_pid in dlc_map and dlc_map[str_pid] != RIMWORLD_STEAM_APP_ID:
        rimworld_pid = CaseInsensitiveStr("ludeon.rimworld")
        if rimworld_pid not in rules.dependencies:
            rules.dependencies[rimworld_pid] = DependencyMod(
                package_id=rimworld_pid,
                name="RimWorld",
                workshop_url="https://store.steampowered.com/app/294100/RimWorld",
            )

    return mod


def _create_about_mod_from_xml(
    base_path: Path,
    mod_xml_path: Path,
    target_version: str,
    prefer_versioned: bool = True,
) -> tuple[bool, AboutXmlMod]:
    """Parse an About.xml file and return a validated AboutXmlMod with its path set."""
    try:
        tree = ET.parse(str(mod_xml_path))
    except (OSError, TypeError):
        logger.error(f"Unable to parse {mod_xml_path}: {traceback.format_exc()}")
        return False, AboutXmlMod(valid=False)

    root = tree.getroot()
    if root is None:
        logger.error(f"Could not parse {mod_xml_path}.")
        return False, AboutXmlMod(valid=False)

    mod = _create_about_mod_from_element(root, target_version, prefer_versioned)
    mod.mod_path = base_path
    return mod.valid, mod


def create_listed_mod_from_path(
    path: Path,
    target_version: str,
    prefer_versioned: bool = True,
    about_xml_path: Path | None = None,
) -> tuple[bool, ListedMod]:
    """Create a ListedMod from a directory path, parsing About.xml if present."""
    if path.is_dir():
        if about_xml_path is None:
            about_xml_path = find_about_xml(path)

        if about_xml_path is not None:
            success, about_mod = _create_about_mod_from_xml(
                path, about_xml_path, target_version, prefer_versioned
            )
            return success, about_mod

        rsc_files = list(path.glob("*.rsc"))
        if len(rsc_files) > 1:
            logger.warning(f"Multiple .rsc files found in {path}. Aborting.")
            return False, ListedMod(valid=False, _mod_path=path)

        if len(rsc_files) == 1:
            # Scenario .rsc files - not implemented in detail yet
            logger.warning(f"Scenario .rsc files not yet supported: {path}")
            return False, ListedMod(valid=False, _mod_path=path)

        logger.warning(f"No About.xml found in directory: {path}")
        return False, ListedMod(valid=False, _mod_path=path)

    raise ValueError(f"Path must be a directory: {path}")


def _get_dlc_packageid_map() -> dict[str, str]:
    return {dlc["packageid"]: appid for appid, dlc in RIMWORLD_DLC_METADATA.items()}
