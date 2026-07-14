from __future__ import annotations

from dataclasses import dataclass

from pxmodrim.models.metadata.structures import CaseInsensitiveStr

PackageId = CaseInsensitiveStr


@dataclass(frozen=True, slots=True)
class CommunityRule:
    package_id: PackageId
    load_after: set[PackageId]
    load_before: set[PackageId]
    load_first: bool
    load_last: bool
    incompatible_with: set[PackageId]
