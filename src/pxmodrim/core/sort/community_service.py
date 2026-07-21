from __future__ import annotations

import os
import zipfile
from pathlib import Path
from typing import TYPE_CHECKING

import httpx
import msgspec

from pxmodrim.core.loading import LoadingState
from pxmodrim.core.sort.community import (
    COMMUNITY_RULES_FILENAME,
    COMMUNITY_RULES_URL,
    ExternalRulesSchema,
    community_rules_path,
    config_dir,
    dec_hook,
)
from pxmodrim.core.sort.models import CommunityRule, PackageId

if TYPE_CHECKING:
    pass


class CommunityRulesService:
    """Service for downloading and loading community rules."""

    __slots__ = ("_cache_dir", "_json_path")

    def __init__(self) -> None:
        self._cache_dir = config_dir()
        self._json_path = community_rules_path()

    async def ensure_rules(
        self, loading: LoadingState, force: bool = False
    ) -> Path | None:
        """Ensure community rules file exists, downloading if needed."""
        if self._json_path.exists() and not force:
            return self._json_path
        return await self._download_rules(loading)

    async def _download_rules(self, loading: LoadingState) -> Path | None:
        self._cache_dir.mkdir(parents=True, exist_ok=True)

        zip_path = self._cache_dir / "community_rules.zip"
        extract_dir = self._cache_dir / "community_rules_extracted"

        try:
            async with httpx.AsyncClient() as client:
                head = await client.head(
                    COMMUNITY_RULES_URL, follow_redirects=True, timeout=10.0
                )
                total_size = int(head.headers.get("content-length", 0)) or 100

                async with client.stream(
                    "GET", COMMUNITY_RULES_URL, follow_redirects=True, timeout=30.0
                ) as resp:
                    resp.raise_for_status()

                    with (
                        loading.task(
                            "Downloading community rules...", total_steps=total_size
                        ) as task,
                        open(zip_path, "wb") as fh,
                    ):
                        async for chunk in resp.aiter_bytes(8192):
                            fh.write(chunk)
                            task.step(len(chunk))

                    with loading.task("Extracting...", total_steps=100) as task:
                        with zipfile.ZipFile(zip_path, "r") as zf:
                            zf.extractall(extract_dir)
                        task.step(50)
                        for root, _dirs, files in os.walk(extract_dir):
                            for filename in files:
                                if filename == COMMUNITY_RULES_FILENAME:
                                    src_path = Path(str(root)) / filename
                                    src_path.replace(self._json_path)
                                    task.step(50)
                                    return self._json_path

        except (OSError, zipfile.BadZipFile, httpx.HTTPError) as e:
            import logging

            logging.getLogger(__name__).warning(
                f"Failed to download community rules: {e}"
            )

        return None

    def load_rules(self) -> dict[PackageId, CommunityRule]:
        """Load community rules from cached file."""
        if not self._json_path.exists():
            return {}
        data = msgspec.json.decode(
            self._json_path.read_bytes(), type=ExternalRulesSchema, dec_hook=dec_hook
        )
        return data.to_community_rules()
