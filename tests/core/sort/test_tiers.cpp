#include <QList>
#include <QSet>
#include <QString>

#include <catch2/catch_test_macros.hpp>

#include "core/sort/tiers.hpp"

using px::modrim::sort::assignTiers;
using px::modrim::sort::PackageGraph;
using px::modrim::sort::PackageId;
using px::modrim::sort::Tier;
using px::modrim::sort::TierConfig;

namespace {

PackageId pid(const QString& value) { return PackageId(value); }

void addEdge(PackageGraph& graph, const QString& from, const QString& to) {
  graph[pid(from)].insert(pid(to));
}

}  // namespace

TEST_CASE("assign_tiers places tier 0 mods and their dependencies in tier 0") {
  PackageGraph deps;
  addEdge(deps, QStringLiteral("zetrith"), QStringLiteral("bar"));
  const QSet<PackageId> canonical{pid(QStringLiteral("zetrith")),
                                  pid(QStringLiteral("bar")),
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
  const QSet<PackageId> canonical{pid(QStringLiteral("late")),
                                  pid(QStringLiteral("dependent")),
                                  pid(QStringLiteral("mid"))};

  const auto tiers =
      assignTiers(canonical, {}, revDeps, {},
                  QSet<PackageId>{pid(QStringLiteral("late"))}, TierConfig{});

  CHECK(tiers.value(pid(QStringLiteral("late"))) == Tier::kTier3);
  CHECK(tiers.value(pid(QStringLiteral("dependent"))) == Tier::kTier3);
  CHECK(tiers.value(pid(QStringLiteral("mid"))) == Tier::kTier2);
}

TEST_CASE("tier 0 overrides tier 3 on overlap") {
  const QSet<PackageId> canonical{pid(QStringLiteral("x"))};
  TierConfig config;
  config.tier_0 = {pid(QStringLiteral("x"))};

  const auto tiers = assignTiers(
      canonical, {}, {}, {}, QSet<PackageId>{pid(QStringLiteral("x"))}, config);

  CHECK(tiers.value(pid(QStringLiteral("x"))) == Tier::kTier0);
}

TEST_CASE("tier 1 only promotes tier 2 mods") {
  const QSet<PackageId> canonical{pid(QStringLiteral("first")),
                                  pid(QStringLiteral("last"))};
  const QSet<PackageId> loadFirst{pid(QStringLiteral("first")),
                                  pid(QStringLiteral("last"))};
  const QSet<PackageId> loadLast{pid(QStringLiteral("last"))};

  const auto tiers =
      assignTiers(canonical, {}, {}, loadFirst, loadLast, TierConfig{});

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
