#include <catch2/catch_test_macros.hpp>

#include <QList>
#include <QSet>
#include <QString>

#include "core/sort/tiers.hpp"

using px::modrim::sort::assign_tiers;
using px::modrim::sort::find_cycle;
using px::modrim::sort::get_deps_recursive;
using px::modrim::sort::get_reverse_deps_recursive;
using px::modrim::sort::PackageGraph;
using px::modrim::sort::PackageId;
using px::modrim::sort::Tier;
using px::modrim::sort::TierConfig;

namespace {

PackageId pid(const QString& value) {
    return PackageId(value);
}

void add_edge(PackageGraph& graph, const QString& from, const QString& to) {
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
    PackageGraph reverse_deps;
    for (int index = 0; index + 1 < kCount; ++index) {
        deps[nodes[index]].insert(nodes[index + 1]);
        reverse_deps[nodes[index + 1]].insert(nodes[index]);
    }

    const QSet<PackageId> tail(nodes.cbegin() + 1, nodes.cend());
    const QSet<PackageId> head(nodes.cbegin(), nodes.cend() - 1);
    CHECK(get_deps_recursive(nodes.front(), deps) == tail);
    CHECK(get_reverse_deps_recursive(nodes.back(), reverse_deps) == head);
    CHECK_FALSE(find_cycle(all, deps).has_value());

    deps[nodes.back()].insert(nodes.front());
    const auto cycle = find_cycle(all, deps);
    REQUIRE(cycle.has_value());
    CHECK(cycle->size() == kCount + 1);
    CHECK(cycle->front() == cycle->back());
    CHECK(QSet<PackageId>(cycle->cbegin(), cycle->cend() - 1) == all);
}

TEST_CASE("find_cycle returns a closed path whose steps are dependency edges") {
    PackageGraph deps;
    add_edge(deps, QStringLiteral("a"), QStringLiteral("b"));
    add_edge(deps, QStringLiteral("b"), QStringLiteral("c"));
    add_edge(deps, QStringLiteral("c"), QStringLiteral("a"));
    add_edge(deps, QStringLiteral("c"), QStringLiteral("d"));
    const QSet<PackageId> nodes{pid(QStringLiteral("a")), pid(QStringLiteral("b")),
                                pid(QStringLiteral("c")), pid(QStringLiteral("d"))};

    const auto cycle = find_cycle(nodes, deps);
    REQUIRE(cycle.has_value());
    REQUIRE(cycle->size() == 4);
    CHECK(cycle->front() == cycle->back());
    for (qsizetype index = 0; index + 1 < cycle->size(); ++index) {
        CHECK(deps.value((*cycle)[index]).contains((*cycle)[index + 1]));
    }
}

TEST_CASE("find_cycle ignores edges that leave the node set") {
    PackageGraph deps;
    add_edge(deps, QStringLiteral("a"), QStringLiteral("outside"));
    add_edge(deps, QStringLiteral("outside"), QStringLiteral("a"));
    const QSet<PackageId> nodes{pid(QStringLiteral("a"))};

    CHECK_FALSE(find_cycle(nodes, deps).has_value());
}

TEST_CASE("assign_tiers places tier 0 mods and their dependencies in tier 0") {
    PackageGraph deps;
    add_edge(deps, QStringLiteral("zetrith"), QStringLiteral("bar"));
    const QSet<PackageId> canonical{pid(QStringLiteral("zetrith")), pid(QStringLiteral("bar")),
                                    pid(QStringLiteral("other"))};
    TierConfig config;
    config.tier_0 = {pid(QStringLiteral("zetrith"))};

    const auto tiers = assign_tiers(canonical, deps, {}, {}, {}, config);

    CHECK(tiers.value(pid(QStringLiteral("zetrith"))) == Tier::Tier0);
    CHECK(tiers.value(pid(QStringLiteral("bar"))) == Tier::Tier0);
    CHECK(tiers.value(pid(QStringLiteral("other"))) == Tier::Tier2);
}

TEST_CASE("assign_tiers sinks load-last mods and their dependents to tier 3") {
    PackageGraph rev_deps;
    add_edge(rev_deps, QStringLiteral("late"), QStringLiteral("dependent"));
    const QSet<PackageId> canonical{pid(QStringLiteral("late")), pid(QStringLiteral("dependent")),
                                    pid(QStringLiteral("mid"))};

    const auto tiers = assign_tiers(canonical, {}, rev_deps, {},
                                    QSet<PackageId>{pid(QStringLiteral("late"))}, TierConfig{});

    CHECK(tiers.value(pid(QStringLiteral("late"))) == Tier::Tier3);
    CHECK(tiers.value(pid(QStringLiteral("dependent"))) == Tier::Tier3);
    CHECK(tiers.value(pid(QStringLiteral("mid"))) == Tier::Tier2);
}

TEST_CASE("tier 0 overrides tier 3 on overlap") {
    const QSet<PackageId> canonical{pid(QStringLiteral("x"))};
    TierConfig config;
    config.tier_0 = {pid(QStringLiteral("x"))};

    const auto tiers = assign_tiers(canonical, {}, {}, {},
                                    QSet<PackageId>{pid(QStringLiteral("x"))}, config);

    CHECK(tiers.value(pid(QStringLiteral("x"))) == Tier::Tier0);
}

TEST_CASE("tier 1 only promotes tier 2 mods") {
    const QSet<PackageId> canonical{pid(QStringLiteral("first")), pid(QStringLiteral("last"))};
    const QSet<PackageId> load_first{pid(QStringLiteral("first")), pid(QStringLiteral("last"))};
    const QSet<PackageId> load_last{pid(QStringLiteral("last"))};

    const auto tiers = assign_tiers(canonical, {}, {}, load_first, load_last, TierConfig{});

    CHECK(tiers.value(pid(QStringLiteral("first"))) == Tier::Tier1);
    CHECK(tiers.value(pid(QStringLiteral("last"))) == Tier::Tier3);
}

TEST_CASE("assign_tiers never returns ids outside the canonical set") {
    const QSet<PackageId> canonical{pid(QStringLiteral("a"))};
    TierConfig config;
    config.tier_0 = {pid(QStringLiteral("ghost"))};
    config.tier_3 = {pid(QStringLiteral("ghost"))};

    const auto tiers = assign_tiers(canonical, {}, {}, {}, {}, config);

    CHECK(tiers.size() == 1);
    CHECK_FALSE(tiers.contains(pid(QStringLiteral("ghost"))));
    CHECK(tiers.value(pid(QStringLiteral("a"))) == Tier::Tier2);
}
