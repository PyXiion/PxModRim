from __future__ import annotations

from dataclasses import dataclass, field
from enum import IntEnum

from pxmodrim.core.models.metadata.structures import CaseInsensitiveStr

PackageId = CaseInsensitiveStr


class Tier(IntEnum):
    TIER_0 = 0
    TIER_1 = 1
    TIER_2 = 2
    TIER_3 = 3


@dataclass(frozen=True, slots=True)
class TierConfig:
    tier_0: tuple[PackageId, ...]
    tier_1: tuple[PackageId, ...]
    tier_3: tuple[PackageId, ...]

    @staticmethod
    def default() -> TierConfig:
        return TierConfig(
            tier_0=(
                PackageId("zetrith.prepatcher"),
                PackageId("brrainz.harmony"),
                PackageId("brrainz.visualexceptions"),
                PackageId("ludeon.rimworld"),
                PackageId("ludeon.rimworld.royalty"),
                PackageId("ludeon.rimworld.ideology"),
                PackageId("ludeon.rimworld.biotech"),
                PackageId("ludeon.rimworld.anomaly"),
                PackageId("ludeon.rimworld.odyssey"),
            ),
            tier_1=(
                PackageId("adaptive.storage.framework"),
                PackageId("aoba.framework"),
                PackageId("aoba.exosuit.framework"),
                PackageId("ebsg.framework"),
                PackageId("imranfish.xmlextensions"),
                PackageId("thesepeople.ritualattachableoutcomes"),
                PackageId("ohno.asf.ab.local"),
                PackageId("oskarpotocki.vanillafactionsexpanded.core"),
                PackageId("owlchemist.cherrypicker"),
                PackageId("redmattis.betterprerequisites"),
                PackageId("smashphil.vehicleframework"),
                PackageId("unlimitedhugs.hugslib"),
                PackageId("vanillaexpanded.backgrounds"),
            ),
            tier_3=(),
        )


@dataclass(frozen=True, slots=True)
class SortSettings:
    use_alternative_package_ids: bool = True
    check_missing_dependencies: bool = True
    use_community_rules: bool = True
    tier_config: TierConfig = field(default_factory=TierConfig.default)
