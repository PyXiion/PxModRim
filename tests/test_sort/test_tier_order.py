from __future__ import annotations

from pxmodrim.checker.graph import ConstraintGraph
from pxmodrim.checker.sort import topological_sort
from pxmodrim.models.metadata.structures import (
    AboutXmlMod,
    CaseInsensitiveSet,
    CaseInsensitiveStr,
)
from pxmodrim.sort.config import SortSettings, Tier, TierConfig

PackageId = CaseInsensitiveStr


def _make_mod(package_id: str) -> AboutXmlMod:
    m = AboutXmlMod()
    m.package_id = CaseInsensitiveStr(package_id)
    m.name = package_id
    m._mod_path = None
    return m


def _graph(
    mods: dict[PackageId, AboutXmlMod],
    ordered_pids: list[PackageId],
    settings: SortSettings | None = None,
) -> ConstraintGraph:
    graph = ConstraintGraph()
    graph.build(mods, ordered_pids, settings or SortSettings())
    return graph


def _active_dict(*mods: AboutXmlMod) -> dict[PackageId, AboutXmlMod]:
    return {PackageId(m.package_id): m for m in mods}


def _sort(
    mods: dict[PackageId, AboutXmlMod],
    tier_config: TierConfig | None = None,
) -> list[PackageId]:
    ordered_pids = list(mods.keys())
    settings = SortSettings(
        use_community_rules=False,
        use_alternative_package_ids=False,
        tier_config=tier_config or TierConfig.default(),
    )
    graph = _graph(mods, ordered_pids, settings)
    return topological_sort(mods, graph, settings)


def _assert_order(
    result: list[PackageId],
    tier: Tier,
    expected: list[str],
):
    pids_in_tier = [pid for pid in result if pid in expected]
    assert [str(p) for p in pids_in_tier] == expected, (
        f"TIER_{tier.value} order mismatch\n"
        f"  expected: {expected}\n"
        f"  got:      {[str(p) for p in pids_in_tier]}"
    )


class TestTier0Order:
    def test_follows_config_order(self):
        mods = _active_dict(
            _make_mod("brrainz.harmony"),
            _make_mod("zetrith.prepatcher"),
            _make_mod("ludeon.rimworld"),
            _make_mod("ludeon.rimworld.royalty"),
            _make_mod("ludeon.rimworld.ideology"),
            _make_mod("ludeon.rimworld.biotech"),
            _make_mod("ludeon.rimworld.anomaly"),
            _make_mod("ludeon.rimworld.odyssey"),
        )
        result = _sort(mods)
        _assert_order(
            result,
            Tier.TIER_0,
            [
                "zetrith.prepatcher",
                "brrainz.harmony",
                "ludeon.rimworld",
                "ludeon.rimworld.royalty",
                "ludeon.rimworld.ideology",
                "ludeon.rimworld.biotech",
                "ludeon.rimworld.anomaly",
                "ludeon.rimworld.odyssey",
            ],
        )

    def test_partial_active(self):
        mods = _active_dict(
            _make_mod("ludeon.rimworld.biotech"),
            _make_mod("zetrith.prepatcher"),
            _make_mod("ludeon.rimworld.royalty"),
        )
        result = _sort(mods)
        _assert_order(
            result,
            Tier.TIER_0,
            [
                "zetrith.prepatcher",
                "ludeon.rimworld.royalty",
                "ludeon.rimworld.biotech",
            ],
        )

    def test_with_deps_within_tier(self):
        mods = _active_dict(
            _make_mod("brrainz.harmony"),
            _make_mod("zetrith.prepatcher"),
        )
        mods[PackageId("brrainz.harmony")].about_rules.load_after = CaseInsensitiveSet(
            ["zetrith.prepatcher"]
        )
        result = _sort(mods)
        _assert_order(
            result,
            Tier.TIER_0,
            [
                "zetrith.prepatcher",
                "brrainz.harmony",
            ],
        )


class TestTier1Order:
    def test_follows_config_order(self):
        mods = _active_dict(
            _make_mod("unlimitedhugs.hugslib"),
            _make_mod("adaptive.storage.framework"),
            _make_mod("aoba.framework"),
            _make_mod("smashphil.vehicleframework"),
            _make_mod("vanillaexpanded.backgrounds"),
        )
        result = _sort(mods)
        _assert_order(
            result,
            Tier.TIER_1,
            [
                "adaptive.storage.framework",
                "aoba.framework",
                "smashphil.vehicleframework",
                "unlimitedhugs.hugslib",
                "vanillaexpanded.backgrounds",
            ],
        )

    def test_partial_active(self):
        mods = _active_dict(
            _make_mod("smashphil.vehicleframework"),
            _make_mod("adaptive.storage.framework"),
            _make_mod("vanillaexpanded.backgrounds"),
        )
        result = _sort(mods)
        _assert_order(
            result,
            Tier.TIER_1,
            [
                "adaptive.storage.framework",
                "smashphil.vehicleframework",
                "vanillaexpanded.backgrounds",
            ],
        )

    def test_with_deps_within_tier(self):
        mods = _active_dict(
            _make_mod("aoba.exosuit.framework"),
            _make_mod("aoba.framework"),
        )
        mods[
            PackageId("aoba.exosuit.framework")
        ].about_rules.load_after = CaseInsensitiveSet(["aoba.framework"])
        result = _sort(mods)
        _assert_order(
            result,
            Tier.TIER_1,
            [
                "aoba.framework",
                "aoba.exosuit.framework",
            ],
        )


class TestTierBoundaries:
    def test_tiers_are_ordered(self):
        mods = _active_dict(
            _make_mod("some.two.mod"),
            _make_mod("brrainz.harmony"),
            _make_mod("adaptive.storage.framework"),
            _make_mod("some.three.mod"),
        )
        tc = TierConfig(
            tier_0=(PackageId("brrainz.harmony"),),
            tier_1=(PackageId("adaptive.storage.framework"),),
            tier_3=(PackageId("some.three.mod"),),
        )
        result = _sort(mods, tier_config=tc)
        order = [str(p) for p in result]
        assert order.index("brrainz.harmony") < order.index(
            "adaptive.storage.framework"
        ), "TIER_0 before TIER_1"
        assert order.index("adaptive.storage.framework") < order.index(
            "some.two.mod"
        ), "TIER_1 before TIER_2"
        assert order.index("some.two.mod") < order.index("some.three.mod"), (
            "TIER_2 before TIER_3"
        )


class TestNonConfigMods:
    def test_unknown_mods_after_config_mods(self):
        mods = _active_dict(
            _make_mod("unknown.first"),
            _make_mod("zetrith.prepatcher"),
            _make_mod("unknown.second"),
            _make_mod("brrainz.harmony"),
        )
        tc = TierConfig(
            tier_0=(PackageId("zetrith.prepatcher"), PackageId("brrainz.harmony")),
            tier_1=(),
            tier_3=(),
        )
        result = _sort(mods, tier_config=tc)
        order = [str(p) for p in result]
        tier0_pids = [
            p for p in order if p in ("zetrith.prepatcher", "brrainz.harmony")
        ]
        assert tier0_pids == ["zetrith.prepatcher", "brrainz.harmony"], (
            f"config mods in wrong order: {tier0_pids}"
        )
