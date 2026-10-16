#include <catch2/catch_test_macros.hpp>

#include <memory>

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include "core/checker/graph.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/sort/config.hpp"

using namespace px::modrim;
using namespace px::modrim::checker;

namespace {

std::shared_ptr<AboutXmlMod> make_mod(const QString& package_id) {
    auto m = std::make_shared<AboutXmlMod>();
    m->package_id = CaseInsensitiveStr(package_id);
    m->name = package_id;
    return m;
}

sort::SortSettings settings(bool use_alternative_package_ids = false) {
    sort::SortSettings s;
    s.use_community_rules = false;
    s.use_alternative_package_ids = use_alternative_package_ids;
    return s;
}

}  // namespace

TEST_CASE("ConstraintGraph empty graph") {
    ConstraintGraph g;
    CHECK(g.nodes().isEmpty());
}

TEST_CASE("ConstraintGraph build no edges") {
    ConstraintGraph g;
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
        {PackageId(QStringLiteral("mod.a")), a},
        {PackageId(QStringLiteral("mod.b")), b},
    };
    g.build(mods, {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))}, settings());
    CHECK(g.has_node(PackageId(QStringLiteral("mod.a"))));
    CHECK(g.has_node(PackageId(QStringLiteral("mod.b"))));
    CHECK(g.nodes().size() == 2);
    CHECK(g.index_of(PackageId(QStringLiteral("mod.a"))) == 0);
    CHECK(g.index_of(PackageId(QStringLiteral("mod.b"))) == 1);
}

TEST_CASE("ConstraintGraph dependency edge") {
    ConstraintGraph g;
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    DependencyMod dep;
    dep.package_id = CaseInsensitiveStr(QStringLiteral("mod.b"));
    a->about_rules.dependencies.append(dep);
    QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
        {PackageId(QStringLiteral("mod.a")), a},
        {PackageId(QStringLiteral("mod.b")), b},
    };
    g.build(mods, {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))}, settings());

    auto edges = g.edges_of_type(PackageId(QStringLiteral("mod.a")), EdgeType::Dependency);
    REQUIRE(edges.size() == 1);
    const ConstraintEdge edge = *edges.cbegin();
    CHECK(edge.source == PackageId(QStringLiteral("mod.a")));
    CHECK(edge.target == PackageId(QStringLiteral("mod.b")));
    CHECK(edge.type == EdgeType::Dependency);
    CHECK(edge.origin == EdgeOrigin::AboutXml);
}

TEST_CASE("ConstraintGraph dependency alternative resolution respects setting") {
    ConstraintGraph g;
    auto consumer = make_mod(QStringLiteral("mod.consumer"));
    auto alternative = make_mod(QStringLiteral("mod.alternative"));
    DependencyMod dependency;
    dependency.package_id = CaseInsensitiveStr(QStringLiteral("mod.dependency"));
    dependency.alternative_package_ids.add(QStringLiteral("mod.alternative"));
    consumer->about_rules.dependencies.append(dependency);

    QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
        {PackageId(QStringLiteral("mod.consumer")), consumer},
        {PackageId(QStringLiteral("mod.alternative")), alternative},
    };
    const QList<PackageId> ordered_pids{
        PackageId(QStringLiteral("mod.consumer")),
        PackageId(QStringLiteral("mod.alternative")),
    };

    g.build(mods, ordered_pids, settings(true));
    QSet<PackageId> enabled_targets;
    for (const auto& edge : g.edges_of_type(PackageId(QStringLiteral("mod.consumer")), EdgeType::Dependency)) {
        enabled_targets.insert(edge.target);
    }
    CHECK(enabled_targets == QSet<PackageId>{PackageId(QStringLiteral("mod.alternative"))});

    g.build(mods, ordered_pids, settings(false));
    QSet<PackageId> disabled_targets;
    for (const auto& edge : g.edges_of_type(PackageId(QStringLiteral("mod.consumer")), EdgeType::Dependency)) {
        disabled_targets.insert(edge.target);
    }
    CHECK(disabled_targets == QSet<PackageId>{PackageId(QStringLiteral("mod.dependency"))});
}

TEST_CASE("ConstraintGraph incompatibility edge") {
    ConstraintGraph g;
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    a->about_rules.incompatible_with.add(QStringLiteral("mod.b"));
    QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
        {PackageId(QStringLiteral("mod.a")), a},
        {PackageId(QStringLiteral("mod.b")), b},
    };
    g.build(mods, {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))}, settings());

    auto edges = g.edges_of_type(PackageId(QStringLiteral("mod.a")), EdgeType::Incompatibility);
    REQUIRE(edges.size() == 1);
    CHECK(edges.cbegin()->target == PackageId(QStringLiteral("mod.b")));
}

TEST_CASE("ConstraintGraph load after edge") {
    ConstraintGraph g;
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    a->about_rules.load_after.add(QStringLiteral("mod.b"));
    QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
        {PackageId(QStringLiteral("mod.a")), a},
        {PackageId(QStringLiteral("mod.b")), b},
    };
    g.build(mods, {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))}, settings());

    auto edges = g.edges_of_type(PackageId(QStringLiteral("mod.a")), EdgeType::LoadAfter);
    REQUIRE(edges.size() == 1);
    CHECK(edges.cbegin()->target == PackageId(QStringLiteral("mod.b")));
}

TEST_CASE("ConstraintGraph load before edge") {
    ConstraintGraph g;
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    a->about_rules.load_before.add(QStringLiteral("mod.b"));
    QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
        {PackageId(QStringLiteral("mod.a")), a},
        {PackageId(QStringLiteral("mod.b")), b},
    };
    g.build(mods, {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))}, settings());

    auto edges = g.edges_of_type(PackageId(QStringLiteral("mod.a")), EdgeType::LoadBefore);
    REQUIRE(edges.size() == 1);
    CHECK(edges.cbegin()->target == PackageId(QStringLiteral("mod.b")));
}

TEST_CASE("ConstraintGraph neighbors") {
    ConstraintGraph g;
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    a->about_rules.incompatible_with.add(QStringLiteral("mod.b"));
    QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
        {PackageId(QStringLiteral("mod.a")), a},
        {PackageId(QStringLiteral("mod.b")), b},
    };
    g.build(mods, {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))}, settings());

    auto nbrs = g.neighbors(PackageId(QStringLiteral("mod.a")));
    CHECK(nbrs.contains(PackageId(QStringLiteral("mod.b"))));
    auto nbrs_b = g.neighbors(PackageId(QStringLiteral("mod.b")));
    CHECK(nbrs_b.contains(PackageId(QStringLiteral("mod.a"))));
}

TEST_CASE("ConstraintGraph incoming") {
    ConstraintGraph g;
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    a->about_rules.incompatible_with.add(QStringLiteral("mod.b"));
    QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
        {PackageId(QStringLiteral("mod.a")), a},
        {PackageId(QStringLiteral("mod.b")), b},
    };
    g.build(mods, {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))}, settings());

    auto inc = g.incoming(PackageId(QStringLiteral("mod.b")));
    REQUIRE(inc.size() == 1);
    CHECK(inc.cbegin()->source == PackageId(QStringLiteral("mod.a")));
}

TEST_CASE("ConstraintGraph remove mod") {
    ConstraintGraph g;
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    a->about_rules.incompatible_with.add(QStringLiteral("mod.b"));
    QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
        {PackageId(QStringLiteral("mod.a")), a},
        {PackageId(QStringLiteral("mod.b")), b},
    };
    g.build(mods, {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))}, settings());
    CHECK(g.has_node(PackageId(QStringLiteral("mod.b"))));

    g.remove_mod(PackageId(QStringLiteral("mod.b")));
    CHECK(!g.has_node(PackageId(QStringLiteral("mod.b"))));
    CHECK(g.outgoing(PackageId(QStringLiteral("mod.a"))).isEmpty());
}

TEST_CASE("ConstraintGraph update order") {
    ConstraintGraph g;
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
        {PackageId(QStringLiteral("mod.a")), a},
        {PackageId(QStringLiteral("mod.b")), b},
    };
    g.build(mods, {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))}, settings());
    CHECK(g.index_of(PackageId(QStringLiteral("mod.a"))) == 0);
    CHECK(g.index_of(PackageId(QStringLiteral("mod.b"))) == 1);

    g.update_order({PackageId(QStringLiteral("mod.b")), PackageId(QStringLiteral("mod.a"))});
    CHECK(g.index_of(PackageId(QStringLiteral("mod.a"))) == 1);
    CHECK(g.index_of(PackageId(QStringLiteral("mod.b"))) == 0);
}

TEST_CASE("ConstraintGraph cycle detection") {
    ConstraintGraph g;
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    auto c = make_mod(QStringLiteral("mod.c"));
    a->about_rules.load_after.add(QStringLiteral("mod.b"));
    b->about_rules.load_after.add(QStringLiteral("mod.c"));
    c->about_rules.load_after.add(QStringLiteral("mod.a"));
    QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
        {PackageId(QStringLiteral("mod.a")), a},
        {PackageId(QStringLiteral("mod.b")), b},
        {PackageId(QStringLiteral("mod.c")), c},
    };
    const QList<PackageId> order{
        PackageId(QStringLiteral("mod.a")),
        PackageId(QStringLiteral("mod.b")),
        PackageId(QStringLiteral("mod.c")),
    };
    g.build(mods, order, settings());
    auto cycles = g.find_cycles();
    REQUIRE(cycles.size() >= 1);
    QSet<PackageId> cycle_pids;
    for (const auto& cycle : cycles) {
        for (const auto& p : cycle) {
            cycle_pids.insert(p);
        }
    }
    for (const auto& name : {QStringLiteral("mod.a"), QStringLiteral("mod.b"), QStringLiteral("mod.c")}) {
        CHECK(cycle_pids.contains(PackageId(name)));
    }
}

TEST_CASE("ConstraintGraph cycle detection reports overlapping cycles") {
    ConstraintGraph g;
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    auto c = make_mod(QStringLiteral("mod.c"));
    a->about_rules.load_after.add(QStringLiteral("mod.b"));
    b->about_rules.load_after.add(QStringLiteral("mod.a"));
    b->about_rules.load_after.add(QStringLiteral("mod.c"));
    c->about_rules.load_after.add(QStringLiteral("mod.b"));
    QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
        {PackageId(QStringLiteral("mod.a")), a},
        {PackageId(QStringLiteral("mod.b")), b},
        {PackageId(QStringLiteral("mod.c")), c},
    };
    const QList<PackageId> order{
        PackageId(QStringLiteral("mod.a")),
        PackageId(QStringLiteral("mod.b")),
        PackageId(QStringLiteral("mod.c")),
    };
    g.build(mods, order, settings());

    auto cycles = g.find_cycles();
    REQUIRE(cycles.size() == 1);
    CHECK(QSet<PackageId>(cycles[0].cbegin(), cycles[0].cend()) ==
          QSet<PackageId>(order.cbegin(), order.cend()));
}

TEST_CASE("ConstraintGraph no cycle") {
    ConstraintGraph g;
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    a->about_rules.load_after.add(QStringLiteral("mod.b"));
    QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
        {PackageId(QStringLiteral("mod.a")), a},
        {PackageId(QStringLiteral("mod.b")), b},
    };
    g.build(mods, {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))}, settings());
    auto cycles = g.find_cycles();
    CHECK(cycles.isEmpty());
}

TEST_CASE("ConstraintGraph cycle detection handles long chain") {
    constexpr int kCount = 5000;
    QList<PackageId> nodes;
    nodes.reserve(kCount);
    QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods;
    for (int i = 0; i < kCount; ++i) {
        PackageId pid(QStringLiteral("mod.%1").arg(i));
        nodes.append(pid);
        mods.insert(pid, make_mod(pid.str()));
    }
    for (int i = 0; i < kCount - 1; ++i) {
        mods[nodes[i]]->about_rules.load_after.add(nodes[i + 1].str());
    }
    ConstraintGraph g;
    g.build(mods, nodes, settings());
    CHECK(g.find_cycles().isEmpty());
}
