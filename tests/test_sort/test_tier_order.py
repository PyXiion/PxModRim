from __future__ import annotations

from pxmodrim.models.metadata.structures import (
    AboutXmlMod,
    CaseInsensitiveSet,
    CaseInsensitiveStr,
)
from pxmodrim.sort.config import SortSettings, Tier, TierConfig
from pxmodrim.sort.graph import build_graphs
from pxmodrim.sort.models import PackageId, SortResult
from pxmodrim.sort.topological import sort_tiers


def _make_mod(package_id: str, uuid: str | None = None) -> AboutXmlMod:
    m = AboutXmlMod()
    m.package_id = CaseInsensitiveStr(package_id)
    m.name = package_id
    m._mod_path = None
    return m


def _mods_dict(*mods: AboutXmlMod) -> dict[str, AboutXmlMod]:
    return {str(i): m for i, m in enumerate(mods)}


def _sort(
    mods: dict[str, AboutXmlMod],
    tier_config: TierConfig | None = None,
) -> SortResult:
    settings = SortSettings(
        use_community_rules=False,
        use_moddependencies_as_load_before=False,
        use_alternative_package_ids=False,
        tier_config=tier_config or TierConfig.default(),
    )
    graphs, diagnostics = build_graphs(mods, settings, community_rules=None)
    return sort_tiers(graphs, diagnostics)


def _assert_order(
    result: SortResult,
    tier: Tier,
    expected: list[str],
):
    pids_in_tier = [pid for pid in result.order if pid in expected]
    assert [str(p) for p in pids_in_tier] == expected, (
        f"TIER_{tier.value} order mismatch\n"
        f"  expected: {expected}\n"
        f"  got:      {[str(p) for p in pids_in_tier]}"
    )


class TestTier0Order:
    """Within TIER_0, mods must follow the tier_config.tier_0 list order."""

    def test_follows_config_order(self):
        """All active tier-0 mods, no edges → output matches config list."""
        mods = _mods_dict(
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
        """Only a subset of config tier-0 mods are active — must preserve relative order."""
        mods = _mods_dict(
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
        """A tier-0 mod depends on another tier-0 mod — dependency constraint must be respected."""
        mods = _mods_dict(
            _make_mod("brrainz.harmony"),
            _make_mod("zetrith.prepatcher"),
        )
        mods["0"].about_rules.load_after = CaseInsensitiveSet(["zetrith.prepatcher"])
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
    """Within TIER_1, mods must follow the tier_config.tier_1 list order."""

    def test_follows_config_order(self):
        """All active tier-1 mods, no edges → output matches config list."""
        mods = _mods_dict(
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
        """Only a subset of config tier-1 mods are active — must preserve relative order."""
        mods = _mods_dict(
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
        """A tier-1 mod depends on another tier-1 mod — dependency constraint must be respected."""
        mods = _mods_dict(
            _make_mod("aoba.exosuit.framework"),
            _make_mod("aoba.framework"),
        )
        mods["0"].about_rules.load_after = CaseInsensitiveSet(["aoba.framework"])
        # aoba.exosuit.framework depends on aoba.framework → aoba.framework must come first
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
    """Tier ordering between tiers must be 0 < 1 < 2 < 3."""

    def test_tiers_are_ordered(self):
        """All mods in TIER_0 must sort before all mods in TIER_1, etc."""
        mods = _mods_dict(
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
        order = [str(p) for p in result.order]
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
    """Mods not in any tier config should sort after config-mod order."""

    def test_unknown_mods_after_config_mods(self):
        """Non-config mods in the same tier come after config-listed mods."""
        mods = _mods_dict(
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
        order = [str(p) for p in result.order]
        tier0_pids = [
            p for p in order if p in ("zetrith.prepatcher", "brrainz.harmony")
        ]
        assert tier0_pids == ["zetrith.prepatcher", "brrainz.harmony"], (
            f"config mods in wrong order: {tier0_pids}"
        )
