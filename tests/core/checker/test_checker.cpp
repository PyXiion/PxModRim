#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <memory>
#include <random>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/checker/checker.hpp"
#include "core/checker/graph.hpp"
#include "core/checker/issues.hpp"
#include "core/checker/models.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/sort/config.hpp"

using namespace px::modrim;
using namespace px::modrim::checker;

namespace {

std::shared_ptr<AboutXmlMod> makeMod(const QString& packageId,
                                     const QString& name = {}) {
  auto m = std::make_shared<AboutXmlMod>();
  m->packageId = CaseInsensitiveStr(packageId);
  m->name = name.isEmpty() ? packageId : name;
  return m;
}

sort::SortSettings settings() {
  sort::SortSettings s;
  s.use_community_rules = false;
  s.use_alternative_package_ids = true;
  s.check_missing_dependencies = true;
  return s;
}

QHash<QString, ListedModPtr> asListed(
    const std::vector<std::shared_ptr<AboutXmlMod>>& mods) {
  QHash<QString, ListedModPtr> result;
  for (std::size_t i = 0; i < mods.size(); ++i) {
    result.insert(QString::number(i), mods[i]);
  }
  return result;
}

QString uuidFor(const QHash<QString, ListedModPtr>& mods, const QString& name) {
  for (auto it = mods.cbegin(); it != mods.cend(); ++it) {
    if (it.value()->name == name) {
      return it.key();
    }
  }
  return {};
}

std::vector<std::unique_ptr<ModIssueChecker>> defaultCheckers() {
  std::vector<std::unique_ptr<ModIssueChecker>> checkers;
  checkers.push_back(std::make_unique<DependencyIssueChecker>());
  checkers.push_back(std::make_unique<IncompatibilityIssueChecker>());
  checkers.push_back(std::make_unique<LoadOrderIssueChecker>());
  checkers.push_back(std::make_unique<CycleIssueChecker>());
  return checkers;
}

}  // namespace

TEST_CASE("ModChecker basic rebuild") {
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  DependencyMod dep;
  dep.packageId = CaseInsensitiveStr(QStringLiteral("mod.c"));
  a->aboutRules.dependencies.append(dep);

  const auto mods = asListed({a, b});
  const QStringList activeUuids{
      uuidFor(mods, QStringLiteral("mod.a")),
      uuidFor(mods, QStringLiteral("mod.b")),
  };

  DiagnosticsByUuid collected;
  auto onChange = [&](const DiagnosticsByUuid& diags) {
    for (auto it = diags.cbegin(); it != diags.cend(); ++it) {
      collected.insert(it.key(), it.value());
    }
  };

  ModChecker checker(defaultCheckers(), settings(), QStringLiteral("1.5"),
                     onChange);
  checker.rebuild(mods, activeUuids);

  const QString aUuid = uuidFor(mods, QStringLiteral("mod.a"));
  const auto* diagA = checker.diagnosticsFor(aUuid);
  REQUIRE(diagA != nullptr);
  CHECK(diagA->hasErrors());
  REQUIRE(diagA->errors.size() == 1);
  CHECK(diagA->errors[0].category == QStringLiteral("missing_dependency"));

  const QString bUuid = uuidFor(mods, QStringLiteral("mod.b"));
  const auto* diagB = checker.diagnosticsFor(bUuid);
  REQUIRE(diagB != nullptr);
  CHECK(!diagB->hasErrors());
  CHECK(!diagB->hasWarnings());
}

TEST_CASE("ModChecker toggle mod rebuilds diagnostics and constraints") {
  std::vector<std::unique_ptr<ModIssueChecker>> checkers;
  checkers.push_back(std::make_unique<DependencyIssueChecker>());
  checkers.push_back(std::make_unique<IncompatibilityIssueChecker>());

  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  DependencyMod dep;
  dep.packageId = CaseInsensitiveStr(QStringLiteral("mod.b"));
  a->aboutRules.dependencies.append(dep);
  b->aboutRules.loadAfter.add(QStringLiteral("mod.a"));

  const auto mods = asListed({a, b});
  const QString aUuid = uuidFor(mods, QStringLiteral("mod.a"));
  const QString bUuid = uuidFor(mods, QStringLiteral("mod.b"));

  ModChecker checker(std::move(checkers), settings());
  checker.rebuild(mods, {aUuid, bUuid});

  checker.toggleMod(*mods[bUuid], {aUuid});
  const auto* diagDisabled = checker.diagnosticsFor(aUuid);
  REQUIRE(diagDisabled != nullptr);
  CHECK(diagDisabled->hasErrors());
  CHECK(diagDisabled->errors[0].category ==
        QStringLiteral("missing_dependency"));

  checker.toggleMod(*mods[bUuid], {aUuid, bUuid});
  const auto* diagEnabled = checker.diagnosticsFor(aUuid);
  REQUIRE(diagEnabled != nullptr);
  CHECK(!diagEnabled->hasErrors());

  QSet<PackageId> dependencySources;
  for (const auto& edge : checker.graph().incomingOfType(
           PackageId(QStringLiteral("mod.b")), EdgeType::kDependency)) {
    dependencySources.insert(edge.source);
  }
  CHECK(dependencySources ==
        QSet<PackageId>{PackageId(QStringLiteral("mod.a"))});

  QSet<PackageId> loadAfterTargets;
  for (const auto& edge : checker.graph().edgesOfType(
           PackageId(QStringLiteral("mod.b")), EdgeType::kLoadAfter)) {
    loadAfterTargets.insert(edge.target);
  }
  CHECK(loadAfterTargets ==
        QSet<PackageId>{PackageId(QStringLiteral("mod.a"))});
}

TEST_CASE("ModChecker move mod updates load order") {
  std::vector<std::unique_ptr<ModIssueChecker>> checkers;
  checkers.push_back(std::make_unique<LoadOrderIssueChecker>());

  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  a->aboutRules.loadAfter.add(QStringLiteral("mod.b"));

  // b first, then a -> satisfies load_after
  const auto mods = asListed({b, a});
  const QString aUuid = uuidFor(mods, QStringLiteral("mod.a"));
  const QString bUuid = uuidFor(mods, QStringLiteral("mod.b"));

  ModChecker checker(std::move(checkers), settings());
  checker.rebuild(mods, {bUuid, aUuid});

  const auto* initial = checker.diagnosticsFor(aUuid);
  CHECK((initial == nullptr || !initial->hasWarnings()));

  // Move a before b (old_index 1 -> new_index 0)
  checker.moveMod(aUuid, 1, 0);

  const auto* afterMove = checker.diagnosticsFor(aUuid);
  REQUIRE(afterMove != nullptr);
  CHECK(afterMove->hasWarnings());
  CHECK(afterMove->warnings[0].category == QStringLiteral("load_order"));
}

TEST_CASE("ModChecker reorder matches full rebuild") {
  std::mt19937 rng(0);
  constexpr int kModCount = 12;
  std::vector<std::shared_ptr<AboutXmlMod>> modsList;
  modsList.reserve(kModCount);
  for (int i = 0; i < kModCount; ++i) {
    modsList.push_back(makeMod(QStringLiteral("mod.%1").arg(i)));
  }

  for (int i = 0; i < kModCount; ++i) {
    std::vector<int> others;
    for (int j = 0; j < kModCount; ++j) {
      if (j != i) {
        others.push_back(j);
      }
    }
    std::ranges::shuffle(others, rng);
    modsList[i]->aboutRules.loadAfter.add(
        QStringLiteral("mod.%1").arg(others[0]));
    modsList[i]->aboutRules.loadAfter.add(
        QStringLiteral("mod.%1").arg(others[1]));
    modsList[i]->aboutRules.loadBefore.add(
        QStringLiteral("mod.%1").arg(others[2]));
  }

  const auto mods = asListed(modsList);
  QStringList order;
  for (int i = 0; i < kModCount; ++i) {
    order.append(QString::number(i));
  }

  ModChecker incremental(defaultCheckers(), settings());
  incremental.rebuild(mods, order);

  for (int step = 0; step < 30; ++step) {
    if (std::uniform_real_distribution<double>(0.0, 1.0)(rng) < 0.5) {
      std::uniform_int_distribution<qsizetype> pick(0, order.size() - 1);
      const qsizetype i = pick(rng);
      const qsizetype j = pick(rng);
      order.swapItemsAt(i, j);
    } else {
      std::uniform_int_distribution<qsizetype> pick(0, order.size() - 1);
      const qsizetype from = pick(rng);
      const qsizetype to = pick(rng);
      order.insert(to, order.takeAt(from));
    }
    incremental.reorder(order);

    ModChecker full(defaultCheckers(), settings());
    full.rebuild(mods, order);

    for (const QString& uuid : order) {
      const auto* got = incremental.diagnosticsFor(uuid);
      const auto* want = full.diagnosticsFor(uuid);
      REQUIRE(got != nullptr);
      REQUIRE(want != nullptr);

      CHECK(got->errors == want->errors);
      CHECK(got->warnings == want->warnings);
    }
  }
}
