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
  CHECK(g.nodes().contains(PackageId(QStringLiteral("mod.a"))));
  CHECK(g.nodes().contains(PackageId(QStringLiteral("mod.b"))));
  CHECK(g.nodes().size() == 2);
  CHECK(g.pidToIndex().value(PackageId(QStringLiteral("mod.a"))) == 0);
  CHECK(g.pidToIndex().value(PackageId(QStringLiteral("mod.b"))) == 1);
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

TEST_CASE("ConstraintGraph resolves alternatives per dependency declaration") {
  ConstraintGraph g;
  const PackageId aPid(QStringLiteral("a.consumer"));
  const PackageId bPid(QStringLiteral("b.consumer"));
  const PackageId primaryPid(QStringLiteral("x"));
  const PackageId yPid(QStringLiteral("y.alt"));
  const PackageId zPid(QStringLiteral("z.alt"));
  auto a = makeMod(aPid.str());
  auto b = makeMod(bPid.str());
  DependencyMod aDependency;
  aDependency.packageId = primaryPid;
  aDependency.alternativePackageIds.add(yPid.str());
  a->aboutRules.dependencies.append(aDependency);
  DependencyMod bDependency;
  bDependency.packageId = primaryPid;
  bDependency.alternativePackageIds.add(zPid.str());
  b->aboutRules.dependencies.append(bDependency);
  QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
      {aPid, a},
      {bPid, b},
      {yPid, makeMod(yPid.str())},
      {zPid, makeMod(zPid.str())},
  };
  PackageId aTarget = yPid;
  PackageId bTarget = zPid;
  auto graphSettings = settings(true);

  SECTION("each consumer uses its own accepted alternative") {}

  SECTION("an active primary takes precedence") {
    mods.insert(primaryPid, makeMod(primaryPid.str()));
    aTarget = primaryPid;
    bTarget = primaryPid;
  }

  SECTION("another consumer's alternative cannot satisfy a declaration") {
    mods.remove(zPid);
    bTarget = primaryPid;
  }

  SECTION("disabled alternative resolution keeps both primary edges") {
    graphSettings.use_alternative_package_ids = false;
    aTarget = primaryPid;
    bTarget = primaryPid;
  }

  g.build(mods, mods.keys(), graphSettings);
  const auto aEdges = g.edgesOfType(aPid, EdgeType::kDependency);
  REQUIRE(aEdges.size() == 1);
  CHECK(aEdges.cbegin()->target == aTarget);
  CHECK(aEdges.cbegin()->origin == EdgeOrigin::kAboutXml);
  const auto bEdges = g.edgesOfType(bPid, EdgeType::kDependency);
  REQUIRE(bEdges.size() == 1);
  CHECK(bEdges.cbegin()->target == bTarget);
  CHECK(bEdges.cbegin()->origin == EdgeOrigin::kAboutXml);
}

TEST_CASE("ConstraintGraph retains alternative resolution for load rules") {
  ConstraintGraph g;
  const PackageId consumerPid(QStringLiteral("mod.consumer"));
  const PackageId primaryPid(QStringLiteral("mod.primary"));
  const PackageId alternativePid(QStringLiteral("mod.alternative"));
  auto consumer = makeMod(consumerPid.str());
  DependencyMod dependency;
  dependency.packageId = primaryPid;
  dependency.alternativePackageIds.add(alternativePid.str());
  consumer->aboutRules.dependencies.append(dependency);
  consumer->aboutRules.loadAfter.add(primaryPid.str());
  sort::CommunityRule rule;
  rule.packageId = consumerPid;
  rule.loadBefore.add(primaryPid.str());
  const sort::CommunityRules communityRules{{consumerPid, rule}};
  const QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods{
      {consumerPid, consumer},
      {alternativePid, makeMod(alternativePid.str())},
  };
  auto graphSettings = settings(true);
  graphSettings.use_community_rules = true;

  g.build(mods, mods.keys(), graphSettings, &communityRules);
  const auto aboutEdges = g.edgesOfType(consumerPid, EdgeType::kLoadAfter);
  REQUIRE(aboutEdges.size() == 1);
  CHECK(aboutEdges.cbegin()->target == alternativePid);
  CHECK(aboutEdges.cbegin()->origin == EdgeOrigin::kAboutXml);
  const auto communityEdges = g.edgesOfType(consumerPid, EdgeType::kLoadBefore);
  REQUIRE(communityEdges.size() == 1);
  CHECK(communityEdges.cbegin()->target == alternativePid);
  CHECK(communityEdges.cbegin()->origin == EdgeOrigin::kCommunityRules);
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
  CHECK(g.pidToIndex().value(PackageId(QStringLiteral("mod.a"))) == 0);
  CHECK(g.pidToIndex().value(PackageId(QStringLiteral("mod.b"))) == 1);

  g.updateOrder(
      {PackageId(QStringLiteral("mod.b")), PackageId(QStringLiteral("mod.a"))});
  CHECK(g.pidToIndex().value(PackageId(QStringLiteral("mod.a"))) == 1);
  CHECK(g.pidToIndex().value(PackageId(QStringLiteral("mod.b"))) == 0);
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
