#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "core/checker/graph.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/sort/config.hpp"

using namespace px::modrim;
using namespace px::modrim::checker;

namespace {

std::shared_ptr<AboutXmlMod> makeMod(const QString& packageId) {
  auto m = std::make_shared<AboutXmlMod>();
  m->packageId = CaseInsensitiveStr(packageId);
  m->name = packageId;
  return m;
}

sort::SortSettings settings(bool useAlternativePackageIds = false) {
  sort::SortSettings s;
  s.use_community_rules = false;
  s.use_alternative_package_ids = useAlternativePackageIds;
  return s;
}

}  // namespace

TEST_CASE("ConstraintGraph empty graph") {
  ConstraintGraph g;
  CHECK(g.nodes().isEmpty());
}

TEST_CASE("ConstraintGraph build no edges") {
  ConstraintGraph g;
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
      {PackageId(QStringLiteral("mod.a")), a},
      {PackageId(QStringLiteral("mod.b")), b},
  };
  g.build(
      mods,
      {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))},
      settings());
  CHECK(g.hasNode(PackageId(QStringLiteral("mod.a"))));
  CHECK(g.hasNode(PackageId(QStringLiteral("mod.b"))));
  CHECK(g.nodes().size() == 2);
  CHECK(g.indexOf(PackageId(QStringLiteral("mod.a"))) == 0);
  CHECK(g.indexOf(PackageId(QStringLiteral("mod.b"))) == 1);
}

TEST_CASE("ConstraintGraph dependency edge") {
  ConstraintGraph g;
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  DependencyMod dep;
  dep.packageId = CaseInsensitiveStr(QStringLiteral("mod.b"));
  a->aboutRules.dependencies.append(dep);
  QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
      {PackageId(QStringLiteral("mod.a")), a},
      {PackageId(QStringLiteral("mod.b")), b},
  };
  g.build(
      mods,
      {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))},
      settings());

  auto edges =
      g.edgesOfType(PackageId(QStringLiteral("mod.a")), EdgeType::kDependency);
  REQUIRE(edges.size() == 1);
  const ConstraintEdge edge = *edges.cbegin();
  CHECK(edge.source == PackageId(QStringLiteral("mod.a")));
  CHECK(edge.target == PackageId(QStringLiteral("mod.b")));
  CHECK(edge.type == EdgeType::kDependency);
  CHECK(edge.origin == EdgeOrigin::kAboutXml);
}

TEST_CASE(
    "ConstraintGraph dependency alternative resolution respects setting") {
  ConstraintGraph g;
  auto consumer = makeMod(QStringLiteral("mod.consumer"));
  auto alternative = makeMod(QStringLiteral("mod.alternative"));
  DependencyMod dependency;
  dependency.packageId = CaseInsensitiveStr(QStringLiteral("mod.dependency"));
  dependency.alternativePackageIds.add(QStringLiteral("mod.alternative"));
  consumer->aboutRules.dependencies.append(dependency);

  QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
      {PackageId(QStringLiteral("mod.consumer")), consumer},
      {PackageId(QStringLiteral("mod.alternative")), alternative},
  };
  const QList<PackageId> orderedPids{
      PackageId(QStringLiteral("mod.consumer")),
      PackageId(QStringLiteral("mod.alternative")),
  };

  g.build(mods, orderedPids, settings(true));
  QSet<PackageId> enabledTargets;
  for (const auto& edge : g.edgesOfType(
           PackageId(QStringLiteral("mod.consumer")), EdgeType::kDependency)) {
    enabledTargets.insert(edge.target);
  }
  CHECK(enabledTargets ==
        QSet<PackageId>{PackageId(QStringLiteral("mod.alternative"))});

  g.build(mods, orderedPids, settings(false));
  QSet<PackageId> disabledTargets;
  for (const auto& edge : g.edgesOfType(
           PackageId(QStringLiteral("mod.consumer")), EdgeType::kDependency)) {
    disabledTargets.insert(edge.target);
  }
  CHECK(disabledTargets ==
        QSet<PackageId>{PackageId(QStringLiteral("mod.dependency"))});
}

TEST_CASE("ConstraintGraph incompatibility edge") {
  ConstraintGraph g;
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  a->aboutRules.incompatibleWith.add(QStringLiteral("mod.b"));
  QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
      {PackageId(QStringLiteral("mod.a")), a},
      {PackageId(QStringLiteral("mod.b")), b},
  };
  g.build(
      mods,
      {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))},
      settings());

  auto edges = g.edgesOfType(PackageId(QStringLiteral("mod.a")),
                             EdgeType::kIncompatibility);
  REQUIRE(edges.size() == 1);
  CHECK(edges.cbegin()->target == PackageId(QStringLiteral("mod.b")));
}

TEST_CASE("ConstraintGraph load after edge") {
  ConstraintGraph g;
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  a->aboutRules.loadAfter.add(QStringLiteral("mod.b"));
  QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
      {PackageId(QStringLiteral("mod.a")), a},
      {PackageId(QStringLiteral("mod.b")), b},
  };
  g.build(
      mods,
      {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))},
      settings());

  auto edges =
      g.edgesOfType(PackageId(QStringLiteral("mod.a")), EdgeType::kLoadAfter);
  REQUIRE(edges.size() == 1);
  CHECK(edges.cbegin()->target == PackageId(QStringLiteral("mod.b")));
}

TEST_CASE("ConstraintGraph load before edge") {
  ConstraintGraph g;
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  a->aboutRules.loadBefore.add(QStringLiteral("mod.b"));
  QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
      {PackageId(QStringLiteral("mod.a")), a},
      {PackageId(QStringLiteral("mod.b")), b},
  };
  g.build(
      mods,
      {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))},
      settings());

  auto edges =
      g.edgesOfType(PackageId(QStringLiteral("mod.a")), EdgeType::kLoadBefore);
  REQUIRE(edges.size() == 1);
  CHECK(edges.cbegin()->target == PackageId(QStringLiteral("mod.b")));
}

TEST_CASE("ConstraintGraph neighbors") {
  ConstraintGraph g;
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  a->aboutRules.incompatibleWith.add(QStringLiteral("mod.b"));
  QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
      {PackageId(QStringLiteral("mod.a")), a},
      {PackageId(QStringLiteral("mod.b")), b},
  };
  g.build(
      mods,
      {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))},
      settings());

  auto nbrs = g.neighbors(PackageId(QStringLiteral("mod.a")));
  CHECK(nbrs.contains(PackageId(QStringLiteral("mod.b"))));
  auto nbrsB = g.neighbors(PackageId(QStringLiteral("mod.b")));
  CHECK(nbrsB.contains(PackageId(QStringLiteral("mod.a"))));
}

TEST_CASE("ConstraintGraph incoming") {
  ConstraintGraph g;
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  a->aboutRules.incompatibleWith.add(QStringLiteral("mod.b"));
  QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
      {PackageId(QStringLiteral("mod.a")), a},
      {PackageId(QStringLiteral("mod.b")), b},
  };
  g.build(
      mods,
      {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))},
      settings());

  auto inc = g.incoming(PackageId(QStringLiteral("mod.b")));
  REQUIRE(inc.size() == 1);
  CHECK(inc.cbegin()->source == PackageId(QStringLiteral("mod.a")));
}

TEST_CASE("ConstraintGraph remove mod") {
  ConstraintGraph g;
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  a->aboutRules.incompatibleWith.add(QStringLiteral("mod.b"));
  QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
      {PackageId(QStringLiteral("mod.a")), a},
      {PackageId(QStringLiteral("mod.b")), b},
  };
  g.build(
      mods,
      {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))},
      settings());
  CHECK(g.hasNode(PackageId(QStringLiteral("mod.b"))));

  g.removeMod(PackageId(QStringLiteral("mod.b")));
  CHECK(!g.hasNode(PackageId(QStringLiteral("mod.b"))));
  CHECK(g.outgoing(PackageId(QStringLiteral("mod.a"))).isEmpty());
}

TEST_CASE("ConstraintGraph update order") {
  ConstraintGraph g;
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
      {PackageId(QStringLiteral("mod.a")), a},
      {PackageId(QStringLiteral("mod.b")), b},
  };
  g.build(
      mods,
      {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))},
      settings());
  CHECK(g.indexOf(PackageId(QStringLiteral("mod.a"))) == 0);
  CHECK(g.indexOf(PackageId(QStringLiteral("mod.b"))) == 1);

  g.updateOrder(
      {PackageId(QStringLiteral("mod.b")), PackageId(QStringLiteral("mod.a"))});
  CHECK(g.indexOf(PackageId(QStringLiteral("mod.a"))) == 1);
  CHECK(g.indexOf(PackageId(QStringLiteral("mod.b"))) == 0);
}

TEST_CASE("ConstraintGraph cycle detection") {
  ConstraintGraph g;
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  auto c = makeMod(QStringLiteral("mod.c"));
  a->aboutRules.loadAfter.add(QStringLiteral("mod.b"));
  b->aboutRules.loadAfter.add(QStringLiteral("mod.c"));
  c->aboutRules.loadAfter.add(QStringLiteral("mod.a"));
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
  auto cycles = g.findCycles();
  REQUIRE(cycles.size() >= 1);
  QSet<PackageId> cyclePids;
  for (const auto& cycle : cycles) {
    for (const auto& p : cycle) {
      cyclePids.insert(p);
    }
  }
  for (const auto& name : {QStringLiteral("mod.a"), QStringLiteral("mod.b"),
                           QStringLiteral("mod.c")}) {
    CHECK(cyclePids.contains(PackageId(name)));
  }
}

TEST_CASE("ConstraintGraph cycle detection reports overlapping cycles") {
  ConstraintGraph g;
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  auto c = makeMod(QStringLiteral("mod.c"));
  a->aboutRules.loadAfter.add(QStringLiteral("mod.b"));
  b->aboutRules.loadAfter.add(QStringLiteral("mod.a"));
  b->aboutRules.loadAfter.add(QStringLiteral("mod.c"));
  c->aboutRules.loadAfter.add(QStringLiteral("mod.b"));
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

  auto cycles = g.findCycles();
  REQUIRE(cycles.size() == 1);
  CHECK(QSet<PackageId>(cycles[0].cbegin(), cycles[0].cend()) ==
        QSet<PackageId>(order.cbegin(), order.cend()));
}

TEST_CASE("ConstraintGraph no cycle") {
  ConstraintGraph g;
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  a->aboutRules.loadAfter.add(QStringLiteral("mod.b"));
  QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
      {PackageId(QStringLiteral("mod.a")), a},
      {PackageId(QStringLiteral("mod.b")), b},
  };
  g.build(
      mods,
      {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))},
      settings());
  auto cycles = g.findCycles();
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
    mods.insert(pid, makeMod(pid.str()));
  }
  for (int i = 0; i < kCount - 1; ++i) {
    mods[nodes[i]]->aboutRules.loadAfter.add(nodes[i + 1].str());
  }
  ConstraintGraph g;
  g.build(mods, nodes, settings());
  CHECK(g.findCycles().isEmpty());
}
