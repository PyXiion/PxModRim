#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <memory>

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/checker/graph.hpp"
#include "core/checker/sort.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/sort/config.hpp"

using px::modrim::AboutXmlMod;
using px::modrim::CaseInsensitiveSet;
using px::modrim::CaseInsensitiveStr;
using px::modrim::checker::ConstraintGraph;
using px::modrim::checker::topologicalSort;
using px::modrim::sort::PackageId;
using px::modrim::sort::SortSettings;
using px::modrim::sort::Tier;
using px::modrim::sort::TierConfig;

namespace {

using ModPtr = std::shared_ptr<AboutXmlMod>;
using ModMap = QHash<PackageId, ModPtr>;

PackageId pid(const QString& value) {
    return PackageId(value);
}

ModPtr makeMod(const QString& packageId) {
    auto mod = std::make_shared<AboutXmlMod>();
    mod->packageId = CaseInsensitiveStr(packageId);
    mod->name = packageId;
    return mod;
}

ModMap activeDict(std::initializer_list<ModPtr> mods) {
    ModMap result;
    for (const auto& mod : mods) {
        result.insert(mod->packageId, mod);
    }
    return result;
}

// Community rules are off and alternative ids are ignored, so only the tier config and explicit
// load-order rules shape the result.
QList<PackageId> sortMods(const ModMap& mods, const TierConfig& tierConfig = TierConfig::defaults()) {
    SortSettings settings;
    settings.use_community_rules = false;
    settings.use_alternative_package_ids = false;
    settings.tier_config = tierConfig;

    ConstraintGraph graph;
    graph.build(mods, mods.keys(), settings);
    return topologicalSort(mods, graph, settings);
}

// Checks only the entries of `result` that belong to `expected`, in their order.
void assertOrder(const QList<PackageId>& result, const QStringList& expected) {
    QStringList inTier;
    for (const auto& entry : result) {
        if (expected.contains(entry.str())) {
            inTier.append(entry.str());
        }
    }
    CHECK(inTier == expected);
}

}  // namespace

TEST_CASE("tier 0 mods follow the config order") {
    const auto mods = activeDict({
        makeMod(QStringLiteral("brrainz.harmony")),
        makeMod(QStringLiteral("zetrith.prepatcher")),
        makeMod(QStringLiteral("ludeon.rimworld")),
        makeMod(QStringLiteral("ludeon.rimworld.royalty")),
        makeMod(QStringLiteral("ludeon.rimworld.ideology")),
        makeMod(QStringLiteral("ludeon.rimworld.biotech")),
        makeMod(QStringLiteral("ludeon.rimworld.anomaly")),
        makeMod(QStringLiteral("ludeon.rimworld.odyssey")),
    });

    assertOrder(sortMods(mods),
                 {QStringLiteral("zetrith.prepatcher"), QStringLiteral("brrainz.harmony"),
                  QStringLiteral("ludeon.rimworld"), QStringLiteral("ludeon.rimworld.royalty"),
                  QStringLiteral("ludeon.rimworld.ideology"), QStringLiteral("ludeon.rimworld.biotech"),
                  QStringLiteral("ludeon.rimworld.anomaly"), QStringLiteral("ludeon.rimworld.odyssey")});
}

TEST_CASE("tier 0 partial active set keeps config order") {
    const auto mods = activeDict({
        makeMod(QStringLiteral("ludeon.rimworld.biotech")),
        makeMod(QStringLiteral("zetrith.prepatcher")),
        makeMod(QStringLiteral("ludeon.rimworld.royalty")),
    });

    assertOrder(sortMods(mods),
                 {QStringLiteral("zetrith.prepatcher"), QStringLiteral("ludeon.rimworld.royalty"),
                  QStringLiteral("ludeon.rimworld.biotech")});
}

TEST_CASE("tier 0 load-after rule orders mods within the tier") {
    const auto mods = activeDict({
        makeMod(QStringLiteral("brrainz.harmony")),
        makeMod(QStringLiteral("zetrith.prepatcher")),
    });
    mods.value(pid(QStringLiteral("brrainz.harmony")))->aboutRules.loadAfter =
        CaseInsensitiveSet(QStringList{QStringLiteral("zetrith.prepatcher")});

    assertOrder(sortMods(mods), {QStringLiteral("zetrith.prepatcher"), QStringLiteral("brrainz.harmony")});
}

TEST_CASE("tier 1 mods follow the config order") {
    const auto mods = activeDict({
        makeMod(QStringLiteral("unlimitedhugs.hugslib")),
        makeMod(QStringLiteral("adaptive.storage.framework")),
        makeMod(QStringLiteral("aoba.framework")),
        makeMod(QStringLiteral("smashphil.vehicleframework")),
        makeMod(QStringLiteral("vanillaexpanded.backgrounds")),
    });

    assertOrder(sortMods(mods),
                 {QStringLiteral("adaptive.storage.framework"), QStringLiteral("aoba.framework"),
                  QStringLiteral("smashphil.vehicleframework"), QStringLiteral("unlimitedhugs.hugslib"),
                  QStringLiteral("vanillaexpanded.backgrounds")});
}

TEST_CASE("tier 1 partial active set keeps config order") {
    const auto mods = activeDict({
        makeMod(QStringLiteral("smashphil.vehicleframework")),
        makeMod(QStringLiteral("adaptive.storage.framework")),
        makeMod(QStringLiteral("vanillaexpanded.backgrounds")),
    });

    assertOrder(sortMods(mods),
                 {QStringLiteral("adaptive.storage.framework"), QStringLiteral("smashphil.vehicleframework"),
                  QStringLiteral("vanillaexpanded.backgrounds")});
}

TEST_CASE("tier 1 load-after rule orders mods within the tier") {
    const auto mods = activeDict({
        makeMod(QStringLiteral("aoba.exosuit.framework")),
        makeMod(QStringLiteral("aoba.framework")),
    });
    mods.value(pid(QStringLiteral("aoba.exosuit.framework")))->aboutRules.loadAfter =
        CaseInsensitiveSet(QStringList{QStringLiteral("aoba.framework")});

    assertOrder(sortMods(mods), {QStringLiteral("aoba.framework"), QStringLiteral("aoba.exosuit.framework")});
}

TEST_CASE("tiers are ordered: 0 before 1 before 2 before 3") {
    const auto mods = activeDict({
        makeMod(QStringLiteral("some.two.mod")),
        makeMod(QStringLiteral("brrainz.harmony")),
        makeMod(QStringLiteral("adaptive.storage.framework")),
        makeMod(QStringLiteral("some.three.mod")),
    });
    TierConfig tierConfig;
    tierConfig.tier_0 = {pid(QStringLiteral("brrainz.harmony"))};
    tierConfig.tier_1 = {pid(QStringLiteral("adaptive.storage.framework"))};
    tierConfig.tier_3 = {pid(QStringLiteral("some.three.mod"))};

    const auto result = sortMods(mods, tierConfig);

    CHECK(result.indexOf(pid(QStringLiteral("brrainz.harmony"))) <
          result.indexOf(pid(QStringLiteral("adaptive.storage.framework"))));
    CHECK(result.indexOf(pid(QStringLiteral("adaptive.storage.framework"))) <
          result.indexOf(pid(QStringLiteral("some.two.mod"))));
    CHECK(result.indexOf(pid(QStringLiteral("some.two.mod"))) <
          result.indexOf(pid(QStringLiteral("some.three.mod"))));
}

TEST_CASE("mods outside every configured tier come after configured mods") {
    const auto mods = activeDict({
        makeMod(QStringLiteral("unknown.first")),
        makeMod(QStringLiteral("zetrith.prepatcher")),
        makeMod(QStringLiteral("unknown.second")),
        makeMod(QStringLiteral("brrainz.harmony")),
    });
    TierConfig tierConfig;
    tierConfig.tier_0 = {pid(QStringLiteral("zetrith.prepatcher")), pid(QStringLiteral("brrainz.harmony"))};

    assertOrder(sortMods(mods, tierConfig),
                 {QStringLiteral("zetrith.prepatcher"), QStringLiteral("brrainz.harmony")});
}
