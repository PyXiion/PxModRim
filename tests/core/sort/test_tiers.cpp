#include <catch2/catch_test_macros.hpp>

#include <QList>
#include <QSet>
#include <QString>

#include "core/sort/tiers.hpp"

using px::modrim::sort::assignTiers;
using px::modrim::sort::findCycle;
using px::modrim::sort::getDepsRecursive;
using px::modrim::sort::getReverseDepsRecursive;
using px::modrim::sort::PackageGraph;
using px::modrim::sort::PackageId;
using px::modrim::sort::Tier;
using px::modrim::sort::TierConfig;

namespace {

PackageId pid(const QString& value) {
    return PackageId(value);
}

void addEdge(PackageGraph& graph, const QString& from, const QString& to) {
    graph[pid(from)].insert(pid(to));
}

}  // namespace

TEST_CASE("tier graph algorithms handle long chains") {
    constexpr int kCount = 5000;
    QList<PackageId> nodes;
    for (int index = 0; index < kCount; ++index) {
        nodes.append(pid(QStringLiteral("mod.%1").arg(index)));
    }
    const QSet<PackageId> all(nodes.cbegin(), nodes.cend());

    PackageGraph deps;
    PackageGraph reverseDeps;
    for (int index = 0; index + 1 < kCount; ++index) {
        deps[nodes[index]].insert(nodes[index + 1]);
        reverseDeps[nodes[index + 1]].insert(nodes[index]);
    }

    const QSet<PackageId> tail(nodes.cbegin() + 1, nodes.cend());
    const QSet<PackageId> head(nodes.cbegin(), nodes.cend() - 1);
    CHECK(getDepsRecursive(nodes.front(), deps) == tail);
    CHECK(getReverseDepsRecursive(nodes.back(), reverseDeps) == head);
    CHECK_FALSE(findCycle(all, deps).has_value());

    deps[nodes.back()].insert(nodes.front());
    const auto cycle = findCycle(all, deps);
    REQUIRE(cycle.has_value());
    CHECK(cycle->size() == kCount + 1);
    CHECK(cycle->front() == cycle->back());
    CHECK(QSet<PackageId>(cycle->cbegin(), cycle->cend() - 1) == all);
}

TEST_CASE("find_cycle returns a closed path whose steps are dependency edges") {
    PackageGraph deps;
    addEdge(deps, QStringLiteral("a"), QStringLiteral("b"));
    addEdge(deps, QStringLiteral("b"), QStringLiteral("c"));
    addEdge(deps, QStringLiteral("c"), QStringLiteral("a"));
    addEdge(deps, QStringLiteral("c"), QStringLiteral("d"));
    const QSet<PackageId> nodes{pid(QStringLiteral("a")), pid(QStringLiteral("b")),
                                pid(QStringLiteral("c")), pid(QStringLiteral("d"))};

    const auto cycle = findCycle(nodes, deps);
    REQUIRE(cycle.has_value());
    REQUIRE(cycle->size() == 4);
    CHECK(cycle->front() == cycle->back());
    for (qsizetype index = 0; index + 1 < cycle->size(); ++index) {
        CHECK(deps.value((*cycle)[index]).contains((*cycle)[index + 1]));
    }
}

TEST_CASE("find_cycle ignores edges that leave the node set") {
    PackageGraph deps;
    addEdge(deps, QStringLiteral("a"), QStringLiteral("outside"));
    addEdge(deps, QStringLiteral("outside"), QStringLiteral("a"));
    const QSet<PackageId> nodes{pid(QStringLiteral("a"))};

    CHECK_FALSE(findCycle(nodes, deps).has_value());
}

TEST_CASE("assign_tiers places tier 0 mods and their dependencies in tier 0") {
    PackageGraph deps;
    addEdge(deps, QStringLiteral("zetrith"), QStringLiteral("bar"));
    const QSet<PackageId> canonical{pid(QStringLiteral("zetrith")), pid(QStringLiteral("bar")),
                                    pid(QStringLiteral("other"))};
    TierConfig config;
    config.tier_0 = {pid(QStringLiteral("zetrith"))};

    const auto tiers = assignTiers(canonical, deps, {}, {}, {}, config);

    CHECK(tiers.value(pid(QStringLiteral("zetrith"))) == Tier::kTier0);
    CHECK(tiers.value(pid(QStringLiteral("bar"))) == Tier::kTier0);
    CHECK(tiers.value(pid(QStringLiteral("other"))) == Tier::kTier2);
}

TEST_CASE("assign_tiers sinks load-last mods and their dependents to tier 3") {
    PackageGraph revDeps;
    addEdge(revDeps, QStringLiteral("late"), QStringLiteral("dependent"));
    const QSet<PackageId> canonical{pid(QStringLiteral("late")), pid(QStringLiteral("dependent")),
                                    pid(QStringLiteral("mid"))};

    const auto tiers = assignTiers(canonical, {}, revDeps, {},
                                    QSet<PackageId>{pid(QStringLiteral("late"))}, TierConfig{});

    CHECK(tiers.value(pid(QStringLiteral("late"))) == Tier::kTier3);
    CHECK(tiers.value(pid(QStringLiteral("dependent"))) == Tier::kTier3);
    CHECK(tiers.value(pid(QStringLiteral("mid"))) == Tier::kTier2);
}

TEST_CASE("tier 0 overrides tier 3 on overlap") {
    const QSet<PackageId> canonical{pid(QStringLiteral("x"))};
    TierConfig config;
    config.tier_0 = {pid(QStringLiteral("x"))};

    const auto tiers = assignTiers(canonical, {}, {}, {},
                                    QSet<PackageId>{pid(QStringLiteral("x"))}, config);

    CHECK(tiers.value(pid(QStringLiteral("x"))) == Tier::kTier0);
}

TEST_CASE("tier 1 only promotes tier 2 mods") {
    const QSet<PackageId> canonical{pid(QStringLiteral("first")), pid(QStringLiteral("last"))};
    const QSet<PackageId> loadFirst{pid(QStringLiteral("first")), pid(QStringLiteral("last"))};
    const QSet<PackageId> loadLast{pid(QStringLiteral("last"))};

    const auto tiers = assignTiers(canonical, {}, {}, loadFirst, loadLast, TierConfig{});

    CHECK(tiers.value(pid(QStringLiteral("first"))) == Tier::kTier1);
    CHECK(tiers.value(pid(QStringLiteral("last"))) == Tier::kTier3);
}

TEST_CASE("assign_tiers never returns ids outside the canonical set") {
    const QSet<PackageId> canonical{pid(QStringLiteral("a"))};
    TierConfig config;
    config.tier_0 = {pid(QStringLiteral("ghost"))};
    config.tier_3 = {pid(QStringLiteral("ghost"))};

    const auto tiers = assignTiers(canonical, {}, {}, {}, {}, config);

    CHECK(tiers.size() == 1);
    CHECK_FALSE(tiers.contains(pid(QStringLiteral("ghost"))));
    CHECK(tiers.value(pid(QStringLiteral("a"))) == Tier::kTier2);
}
