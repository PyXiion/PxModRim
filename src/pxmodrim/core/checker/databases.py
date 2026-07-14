from __future__ import annotations

import json
import zipfile
from collections.abc import Mapping
from pathlib import Path

import httpx
from loguru import logger

from pxmodrim.core.xml import xml_path_to_json
from pxmodrim.core.checker.models import PackageId, ReplacementInfo
from pxmodrim.core.sort.community import config_dir as checker_config_dir

NO_VERSION_WARNING_URL = (
    "https://github.com/emipa606/NoVersionWarning/archive/refs/heads/main.zip"
)
USE_THIS_INSTEAD_URL = (
    "https://github.com/emipa606/UseThisInstead/archive/refs/heads/main.zip"
)


def no_version_warning_path() -> Path:
    return checker_config_dir() / "ModIdsToFix.xml"


def use_this_instead_path() -> Path:
    return checker_config_dir() / "replacements.json.gz"


class NoVersionWarningService:
    """Service to download and cache the NoVersionWarning package-ID list."""

    def __init__(self) -> None:
        self._cache_dir = checker_config_dir()
        self._xml_path = no_version_warning_path()
        self._pids: set[PackageId] = set()

    async def ensure(self, force: bool = False) -> set[PackageId]:
        """Return cached PIDs, or download and cache them if missing or forced."""
        if self._xml_path.exists() and not force:
            self._pids = self._load()
            return self._pids
        return await self._download()

    def load_if_exists(self) -> set[PackageId]:
        """Load cached PIDs if the local file exists, otherwise return empty set."""
        if self._xml_path.exists():
            self._pids = self._load()
        return self._pids

    def _load(self) -> set[PackageId]:
        """Parse the cached XML file into a set of PackageIds."""
        try:
            data = xml_path_to_json(str(self._xml_path))
            mod_ids = data.get("ModIdsToFix", {}).get("li", [])
            if isinstance(mod_ids, str):
                mod_ids = [mod_ids]
            result: set[PackageId] = {PackageId(mid) for mid in mod_ids}
            logger.info(f"Loaded {len(result)} No Version Warning entries")
            return result
        except Exception as e:
            logger.error(f"Failed to load No Version Warning DB: {e}")
            return set()

    async def _download(self) -> set[PackageId]:
        """Download, extract, and cache the NoVersionWarning database from GitHub."""
        self._cache_dir.mkdir(parents=True, exist_ok=True)
        zip_path = self._cache_dir / "no_version_warning.zip"
        extract_dir = self._cache_dir / "no_version_warning_extracted"

        try:
            async with httpx.AsyncClient() as client, client.stream(
                "GET", NO_VERSION_WARNING_URL, follow_redirects=True, timeout=30.0
            ) as resp:
                resp.raise_for_status()
                with open(zip_path, "wb") as fh:
                    async for chunk in resp.aiter_bytes(8192):
                        fh.write(chunk)

            with zipfile.ZipFile(zip_path, "r") as zf:
                zf.extractall(extract_dir)

            for root, _dirs, files in extract_dir.walk():
                for filename in files:
                    if filename == "ModIdsToFix.xml":
                        src_path = root / filename
                        src_path.replace(self._xml_path)
                        self._pids = self._load()
                        # Cleanup
                        _rmtree(zip_path, extract_dir)
                        return self._pids

        except Exception as e:
            logger.warning(f"Failed to download No Version Warning DB: {e}")

        _rmtree(zip_path, extract_dir)
        return set()


class UseThisInsteadService:
    """Service to download and cache the UseThisInstead replacement database."""

    def __init__(self) -> None:
        self._cache_dir = checker_config_dir()
        self._json_path = use_this_instead_path()
        self._entries: dict[str, ReplacementInfo] = {}

    async def ensure(self, force: bool = False) -> Mapping[str, ReplacementInfo]:
        """Return cached replacement entries, download if missing or forced."""
        if self._json_path.exists() and not force:
            self._entries = self._load()
            return self._entries
        return await self._download()

    def load_if_exists(self) -> Mapping[str, ReplacementInfo]:
        """Load cached replacement entries if local file exists, else empty dict."""
        if self._json_path.exists():
            self._entries = self._load()
        return self._entries

    def _load(self) -> dict[str, ReplacementInfo]:
        """Parse cached JSON into a dict of old-Workshop-ID to ReplacementInfo."""
        try:
            path = self._json_path
            if str(path).endswith(".gz"):
                import gzip

                with gzip.open(path, "rt", encoding="utf-8-sig") as f:
                    raw = json.load(f)
            else:
                with open(path, encoding="utf-8-sig") as f:
                    raw = json.load(f)

            rules = raw.get("rules", []) if isinstance(raw, dict) else []
            result: dict[str, ReplacementInfo] = {}
            for r in rules:
                if isinstance(r, dict) and "oldWorkshopId" in r:
                    result[str(r["oldWorkshopId"])] = ReplacementInfo(
                        name=r.get("newName", ""),
                        author=r.get("newAuthor", ""),
                        packageid=r.get("newPackageId", ""),
                        pfid=r.get("newWorkshopId", ""),
                        supportedversions=r.get("newVersions", []),
                        source="database",
                    )
            logger.info(f"Loaded {len(result)} Use This Instead entries")
            return result
        except Exception as e:
            logger.error(f"Failed to load Use This Instead DB: {e}")
            return {}

    async def _download(self) -> dict[str, ReplacementInfo]:
        """Download, extract, and cache the UseThisInstead database from GitHub."""
        self._cache_dir.mkdir(parents=True, exist_ok=True)
        zip_path = self._cache_dir / "use_this_instead.zip"
        extract_dir = self._cache_dir / "use_this_instead_extracted"

        try:
            async with httpx.AsyncClient() as client, client.stream(
                "GET", USE_THIS_INSTEAD_URL, follow_redirects=True, timeout=30.0
            ) as resp:
                resp.raise_for_status()
                with open(zip_path, "wb") as fh:
                    async for chunk in resp.aiter_bytes(8192):
                        fh.write(chunk)

            with zipfile.ZipFile(zip_path, "r") as zf:
                zf.extractall(extract_dir)

            for root, _dirs, files in extract_dir.walk():
                for filename in files:
                    if filename == "replacements.json.gz":
                        src_path = root / filename
                        src_path.replace(self._json_path)
                        self._entries = self._load()
                        _rmtree(zip_path, extract_dir)
                        return self._entries

        except Exception as e:
            logger.warning(f"Failed to download Use This Instead DB: {e}")

        _rmtree(zip_path, extract_dir)
        return {}


def _rmtree(zip_path: Path, extract_dir: Path) -> None:
    try:
        if zip_path.exists():
            zip_path.unlink()
    except OSError:
        pass
    try:
        if extract_dir.exists():
            import shutil

            shutil.rmtree(extract_dir)
    except OSError:
        pass
