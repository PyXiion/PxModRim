import concurrent.futures
import gzip
import json
import os
from collections.abc import Callable
from pathlib import Path
from typing import Any

from loguru import logger

from app.models.metadata.metadata_factory import (
    parse_mod_batch,
    read_rules_db,
    read_steam_db,
)
from app.models.metadata.metadata_structure import (
    ExternalRulesSchema,
    ListedMod,
    SteamDbSchema,
)
from app.utils.xml import xml_path_to_json


class MetadataMediator:
    "Mediator class for metadata."

    def __init__(
        self,
        user_rules_path: Path,
        community_rules_path: Path | None,
        steam_db_path: Path | None,
        workshop_mods_path: Path | None,
        local_mods_path: Path | None,
        game_path: Path | None,
        no_version_warning_path: Path | None = None,
        use_this_instead_path: Path | None = None,
    ):
        self.user_rules_path = user_rules_path
        self.community_rules_path = community_rules_path
        self.steam_db_path = steam_db_path
        self.workshop_mods_path = workshop_mods_path
        self.local_mods_path = local_mods_path
        self.game_path = game_path
        self.no_version_warning_path = no_version_warning_path
        self.use_this_instead_path = use_this_instead_path

        self._user_rules: ExternalRulesSchema | None = None
        self._community_rules: ExternalRulesSchema | None = None
        self._steam_db: SteamDbSchema | None = None
        self._mods_metadata: dict[str, ListedMod] | None = None
        self._game_version: str = "Unknown"
        self._no_version_warning: list[str] | None = None
        self._use_this_instead: dict[str, Any] | None = None

    @property
    def user_rules(self) -> ExternalRulesSchema | None:
        return self._user_rules

    @property
    def community_rules(self) -> ExternalRulesSchema | None:
        return self._community_rules

    @property
    def steam_db(self) -> SteamDbSchema | None:
        return self._steam_db

    @property
    def mods_metadata(self) -> dict[str, ListedMod]:
        """Mods_metadata is a dict representation of all the listedmods, where the key is
        the path to the mod.

        Returns an empty dict when metadata has not been loaded yet.
        :return: A dict of ListedMods keyed by mod path.
        :rtype: dict[str, ListedMod]
        """
        if self._mods_metadata is None:
            return {}
        return self._mods_metadata

    @property
    def game_modules_path(self) -> Path:
        if self.game_path is not None:
            return self.game_path / "Data"
        raise ValueError("Game path is not set")

    @property
    def game_version(self) -> str:
        return self._game_version

    @property
    def no_version_warning(self) -> list[str] | None:
        return self._no_version_warning

    @property
    def use_this_instead(self) -> dict[str, Any] | None:
        return self._use_this_instead

    def _load_no_version_warning(self) -> None:
        """Load No Version Warning DB (ModIdsToFix.xml)."""
        if self.no_version_warning_path is None:
            self._no_version_warning = None
            return

        if not self.no_version_warning_path.exists() and self.game_version != "Unknown":
            game_major_minor = ".".join(self.game_version.split(".")[:2])
            versioned = (
                self.no_version_warning_path.parent
                / game_major_minor
                / self.no_version_warning_path.name
            )
            if versioned.exists():
                self.no_version_warning_path = versioned

        if not self.no_version_warning_path.exists():
            self._no_version_warning = None
            return
        try:
            data = xml_path_to_json(str(self.no_version_warning_path))
            mod_ids = data.get("ModIdsToFix", {}).get("li", [])
            if isinstance(mod_ids, str):
                mod_ids = [mod_ids]
            self._no_version_warning = [str(mid).lower() for mid in mod_ids]
            logger.info(
                f"Loaded {len(self._no_version_warning)} No Version Warning entries"
            )
        except (OSError, ValueError, KeyError) as e:
            logger.error(f"Failed to load No Version Warning DB: {e}")
            self._no_version_warning = None

    def _load_use_this_instead(self) -> None:
        """Load Use This Instead replacements DB (JSON, possibly gzip).

        The raw file is ``{"version": "...", "rules": [...]}``.  We index the
        rules list into a dict keyed by ``oldWorkshopId`` for O(1) lookup.
        """
        if self.use_this_instead_path is None:
            logger.debug("Use This Instead path not configured")
            self._use_this_instead = None
            return
        if not self.use_this_instead_path.exists():
            logger.warning(
                f"Use This Instead DB not found at: {self.use_this_instead_path}"
            )
            self._use_this_instead = None
            return
        try:
            path = self.use_this_instead_path
            if str(path).endswith(".gz"):
                with gzip.open(path, "rt", encoding="utf-8-sig") as f:
                    raw = json.load(f)
            else:
                with open(path, encoding="utf-8-sig") as f:
                    raw = json.load(f)

            rules = raw.get("rules", []) if isinstance(raw, dict) else []
            self._use_this_instead = {
                str(r["oldWorkshopId"]): r
                for r in rules
                if isinstance(r, dict) and "oldWorkshopId" in r
            }
            logger.info(
                f"Loaded {len(self._use_this_instead)} Use This Instead entries"
            )
        except (OSError, ValueError, json.JSONDecodeError) as e:
            logger.error(f"Failed to load Use This Instead DB: {e}")
            self._use_this_instead = None

    def refresh_metadata(
        self,
        prefer_versioned: bool = True,
        case_insensitive_about_xml: bool = True,
        progress_callback: Callable[[int, int], None] | None = None,
    ) -> None:
        """Force refreshes the internal metadata.

        :param prefer_versioned: When True (default), ByVersion keys in mod
            About.xml override base values non-additively. When False, all
            ByVersion keys are ignored and only base values are used.
        :param case_insensitive_about_xml: When True (default), use case-insensitive
            About.xml lookup. When False, require exact "About/About.xml" path.
        :param progress_callback: Optional callback(current, total) for progress.
        """

        for path in {self.local_mods_path, self.game_path}:
            if path is None or not path.exists() or not path.is_dir():
                logger.warning(
                    "Essential paths are missing, invalid, or not directories. "
                    "Skipping metadata refresh."
                )
                return

        self._refresh_game_version()

        self._user_rules = read_rules_db(self.user_rules_path)

        self._community_rules = (
            read_rules_db(self.community_rules_path)
            if self.community_rules_path is not None
            else None
        )
        self._steam_db = (
            read_steam_db(self.steam_db_path)
            if self.steam_db_path is not None
            else None
        )

        # Load additional external metadata
        self._load_no_version_warning()
        self._load_use_this_instead()

        # Get all folders in the workshop and local mods paths
        mod_paths: list[Path] = []
        for search_path in (
            self.workshop_mods_path,
            self.local_mods_path,
            self.game_modules_path,
        ):
            if search_path is None:
                continue
            if not search_path.exists():
                logger.warning(f"Mod search path does not exist: {search_path}")
                continue
            mod_paths.extend(p for p in search_path.iterdir() if p.is_dir())

        assert self.local_mods_path is not None
        assert self.game_path is not None

        logger.debug(f"Parsing {len(mod_paths)} mods")
        str_paths = [str(p) for p in mod_paths]
        if not str_paths:
            self._mods_metadata = {}
            logger.info("No mods found to parse")
            return

        threads = max(os.cpu_count() or 4, 1)
        batch_size = max(len(str_paths) // threads, 1)
        batches = [
            str_paths[i : i + batch_size] for i in range(0, len(str_paths), batch_size)
        ]
        logger.debug(
            f"Parsing {len(str_paths)} mods across {len(batches)} batches "
            f"({threads} threads)"
        )

        self._mods_metadata = {}
        total_mods = len(str_paths)
        completed = 0
        with concurrent.futures.ThreadPoolExecutor(max_workers=threads) as executor:
            fut_to_batch = {
                executor.submit(
                    parse_mod_batch,
                    batch,
                    self.game_version,
                    str(self.local_mods_path),
                    str(self.game_path),
                    str(self.workshop_mods_path) if self.workshop_mods_path else None,
                    self.user_rules,
                    self.community_rules,
                    self.steam_db,
                    prefer_versioned,
                    case_insensitive_about_xml,
                ): batch
                for batch in batches
            }
            for future in concurrent.futures.as_completed(fut_to_batch):
                try:
                    self._mods_metadata.update(future.result())
                except Exception as e:
                    logger.error(
                        f"Batch parsing failed for {len(fut_to_batch[future])} mods: {e}"
                    )
                completed += len(fut_to_batch[future])
                if progress_callback is not None:
                    progress_callback(completed, total_mods)
        logger.info(f"Metadata refresh complete, found {len(self._mods_metadata)} mods")
        return

    def _refresh_game_version(self) -> bool:
        # Get & set Rimworld version string
        if self.game_path is None:
            self._game_version = "Unknown"
            return False

        version_file_path = str(self.game_path / "Version.txt")
        if os.path.exists(version_file_path):
            try:
                with open(version_file_path, encoding="utf-8") as f:
                    self._game_version = f.read().strip()
                    logger.info(
                        f"Retrieved game version from Version.txt: {self.game_version}"
                    )
                    return True
            except Exception:
                logger.error(
                    f"Unable to parse Version.txt from game folder: {version_file_path}"
                )
            self._game_version = "Unknown"
            return False

        else:
            logger.error(
                f"The provided Version.txt path does not exist: {version_file_path}"
            )
            self._game_version = "Unknown"
            return False
