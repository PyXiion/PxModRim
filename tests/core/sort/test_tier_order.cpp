#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <memory>

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/checker/graph.h"
#include "core/checker/sort.h"
#include "core/models/metadata/structures.h"
#include "core/sort/config.h"

using px::core::AboutXmlMod;
using px::core::CaseInsensitiveSet;
using px::core::CaseInsensitiveStr;
using px::core::checker::ConstraintGraph;
using px::core::checker::topological_sort;
using px::core::sort::PackageId;
using px::core::sort::SortSettings;
using px::core::sort::Tier;
using px::core::sort::TierConfig;

namespace {

using ModPtr = std::shared_ptr<AboutXmlMod>;
using ModMap = QHash<PackageId, ModPtr>;

PackageId pid(const QString& value) {
    return PackageId(value);
}

ModPtr make_mod(const QString& package_id) {
    auto mod = std::make_shared<AboutXmlMod>();
    mod->package_id = CaseInsensitiveStr(package_id);
    mod->name = package_id;
    return mod;
}

ModMap active_dict(std::initializer_list<ModPtr> mods) {
    ModMap result;
    for (const auto& mod : mods) {
        result.insert(mod->package_id, mod);
    }
    return result;
}

// Community rules are off and alternative ids are ignored, so only the tier config and explicit
// load-order rules shape the result.
QList<PackageId> sort_mods(const ModMap& mods, const TierConfig& tier_config = TierConfig::defaults()) {
    SortSettings settings;
    settings.use_community_rules = false;
    settings.use_alternative_package_ids = false;
    settings.tier_config = tier_config;

    ConstraintGraph graph;
    graph.build(mods, mods.keys(), settings);
    return topological_sort(mods, graph, settings);
}

// Checks only the entries of `result` that belong to `expected`, in their order.
void assert_order(const QList<PackageId>& result, const QStringList& expected) {
    QStringList in_tier;
    for (const auto& entry : result) {
        if (expected.contains(entry.str())) {
            in_tier.append(entry.str());
        }
    }
    CHECK(in_tier == expected);
}

}  // namespace

TEST_CASE("tier 0 mods follow the config order") {
    const auto mods = active_dict({
        make_mod(QStringLiteral("brrainz.harmony")),
        make_mod(QStringLiteral("zetrith.prepatcher")),
        make_mod(QStringLiteral("ludeon.rimworld")),
        make_mod(QStringLiteral("ludeon.rimworld.royalty")),
        make_mod(QStringLiteral("ludeon.rimworld.ideology")),
        make_mod(QStringLiteral("ludeon.rimworld.biotech")),
        make_mod(QStringLiteral("ludeon.rimworld.anomaly")),
        make_mod(QStringLiteral("ludeon.rimworld.odyssey")),
    });

    assert_order(sort_mods(mods),
                 {QStringLiteral("zetrith.prepatcher"), QStringLiteral("brrainz.harmony"),
                  QStringLiteral("ludeon.rimworld"), QStringLiteral("ludeon.rimworld.royalty"),
                  QStringLiteral("ludeon.rimworld.ideology"), QStringLiteral("ludeon.rimworld.biotech"),
                  QStringLiteral("ludeon.rimworld.anomaly"), QStringLiteral("ludeon.rimworld.odyssey")});
}

TEST_CASE("tier 0 partial active set keeps config order") {
    const auto mods = active_dict({
        make_mod(QStringLiteral("ludeon.rimworld.biotech")),
        make_mod(QStringLiteral("zetrith.prepatcher")),
        make_mod(QStringLiteral("ludeon.rimworld.royalty")),
    });

    assert_order(sort_mods(mods),
                 {QStringLiteral("zetrith.prepatcher"), QStringLiteral("ludeon.rimworld.royalty"),
                  QStringLiteral("ludeon.rimworld.biotech")});
}

TEST_CASE("tier 0 load-after rule orders mods within the tier") {
    const auto mods = active_dict({
        make_mod(QStringLiteral("brrainz.harmony")),
        make_mod(QStringLiteral("zetrith.prepatcher")),
    });
    mods.value(pid(QStringLiteral("brrainz.harmony")))->about_rules.load_after =
        CaseInsensitiveSet(QStringList{QStringLiteral("zetrith.prepatcher")});

    assert_order(sort_mods(mods), {QStringLiteral("zetrith.prepatcher"), QStringLiteral("brrainz.harmony")});
}

TEST_CASE("tier 1 mods follow the config order") {
    const auto mods = active_dict({
        make_mod(QStringLiteral("unlimitedhugs.hugslib")),
        make_mod(QStringLiteral("adaptive.storage.framework")),
        make_mod(QStringLiteral("aoba.framework")),
        make_mod(QStringLiteral("smashphil.vehicleframework")),
        make_mod(QStringLiteral("vanillaexpanded.backgrounds")),
    });

    assert_order(sort_mods(mods),
                 {QStringLiteral("adaptive.storage.framework"), QStringLiteral("aoba.framework"),
                  QStringLiteral("smashphil.vehicleframework"), QStringLiteral("unlimitedhugs.hugslib"),
                  QStringLiteral("vanillaexpanded.backgrounds")});
}

TEST_CASE("tier 1 partial active set keeps config order") {
    const auto mods = active_dict({
        make_mod(QStringLiteral("smashphil.vehicleframework")),
        make_mod(QStringLiteral("adaptive.storage.framework")),
        make_mod(QStringLiteral("vanillaexpanded.backgrounds")),
    });

    assert_order(sort_mods(mods),
                 {QStringLiteral("adaptive.storage.framework"), QStringLiteral("smashphil.vehicleframework"),
                  QStringLiteral("vanillaexpanded.backgrounds")});
}

TEST_CASE("tier 1 load-after rule orders mods within the tier") {
    const auto mods = active_dict({
        make_mod(QStringLiteral("aoba.exosuit.framework")),
        make_mod(QStringLiteral("aoba.framework")),
    });
    mods.value(pid(QStringLiteral("aoba.exosuit.framework")))->about_rules.load_after =
        CaseInsensitiveSet(QStringList{QStringLiteral("aoba.framework")});

    assert_order(sort_mods(mods), {QStringLiteral("aoba.framework"), QStringLiteral("aoba.exosuit.framework")});
}

TEST_CASE("tiers are ordered: 0 before 1 before 2 before 3") {
    const auto mods = active_dict({
        make_mod(QStringLiteral("some.two.mod")),
        make_mod(QStringLiteral("brrainz.harmony")),
        make_mod(QStringLiteral("adaptive.storage.framework")),
        make_mod(QStringLiteral("some.three.mod")),
    });
    TierConfig tier_config;
    tier_config.tier_0 = {pid(QStringLiteral("brrainz.harmony"))};
    tier_config.tier_1 = {pid(QStringLiteral("adaptive.storage.framework"))};
    tier_config.tier_3 = {pid(QStringLiteral("some.three.mod"))};

    const auto result = sort_mods(mods, tier_config);

    CHECK(result.indexOf(pid(QStringLiteral("brrainz.harmony"))) <
          result.indexOf(pid(QStringLiteral("adaptive.storage.framework"))));
    CHECK(result.indexOf(pid(QStringLiteral("adaptive.storage.framework"))) <
          result.indexOf(pid(QStringLiteral("some.two.mod"))));
    CHECK(result.indexOf(pid(QStringLiteral("some.two.mod"))) <
          result.indexOf(pid(QStringLiteral("some.three.mod"))));
}

TEST_CASE("mods outside every configured tier come after configured mods") {
    const auto mods = active_dict({
        make_mod(QStringLiteral("unknown.first")),
        make_mod(QStringLiteral("zetrith.prepatcher")),
        make_mod(QStringLiteral("unknown.second")),
        make_mod(QStringLiteral("brrainz.harmony")),
    });
    TierConfig tier_config;
    tier_config.tier_0 = {pid(QStringLiteral("zetrith.prepatcher")), pid(QStringLiteral("brrainz.harmony"))};

    assert_order(sort_mods(mods, tier_config),
                 {QStringLiteral("zetrith.prepatcher"), QStringLiteral("brrainz.harmony")});
}
