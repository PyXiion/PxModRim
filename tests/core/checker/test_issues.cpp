#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "core/checker/graph.hpp"
#include "core/checker/issues.hpp"
#include "core/checker/models.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/sort/config.hpp"
#include "core/sort/models.hpp"

using namespace px::modrim;
using namespace px::modrim::checker;

namespace {

std::shared_ptr<AboutXmlMod> makeMod(const QString& packageId) {
  auto m = std::make_shared<AboutXmlMod>();
  m->packageId = CaseInsensitiveStr(packageId);
  m->name = packageId;
  return m;
}

sort::SortSettings defaultSettings() {
  sort::SortSettings s;
  s.use_community_rules = false;
  s.use_alternative_package_ids = true;
  s.check_missing_dependencies = true;
  return s;
}

// Holds the storage referenced by a CheckContext so tests can construct one
// simply.
struct ContextStorage {
  QHash<PackageId, std::shared_ptr<AboutXmlMod>> activeMods;
  QList<PackageId> orderedPids;
  QHash<PackageId, int> pidToIndex;
  ConstraintGraph graph;
  sort::SortSettings settings = defaultSettings();
  QString targetVersion = QStringLiteral("1.5");
  QSet<PackageId> noVersionWarning;
  QHash<QString, ReplacementInfo> useThisInstead;
  QList<QList<PackageId>> cycles;

  CheckContext ctx() const {
    return CheckContext{
        .activeMods = activeMods,
        .orderedPids = orderedPids,
        .pidToIndex = pidToIndex,
        .graph = graph,
        .settings = settings,
        .targetVersion = targetVersion,
        .noVersionWarning = noVersionWarning,
        .useThisInstead = useThisInstead,
        .cycles = cycles,
    };
  }
};

ContextStorage makeStorage(
    QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods,
    std::optional<QList<PackageId>> ordered = std::nullopt,
    const sort::SortSettings* settings = nullptr,
    const sort::CommunityRules* communityRules = nullptr) {
  ContextStorage storage;
  storage.activeMods = std::move(mods);
  storage.orderedPids = ordered ? *ordered : storage.activeMods.keys();
  if (settings != nullptr) {
    storage.settings = *settings;
  }
  for (qsizetype i = 0; i < storage.orderedPids.size(); ++i) {
    storage.pidToIndex.insert(storage.orderedPids[i], static_cast<int>(i));
  }
  storage.graph.build(storage.activeMods, storage.orderedPids, storage.settings,
                      communityRules);
  return storage;
}

}  // namespace

TEST_CASE("DependencyIssueChecker no issues when mod has no dependencies") {
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  auto storage = makeStorage({{a->packageId, a}, {b->packageId, b}});
  DependencyIssueChecker checker;
  CHECK(!checker.shouldCheck(*a, storage.ctx()));
  CHECK(checker.check(*a, storage.ctx()).isEmpty());
}

TEST_CASE("DependencyIssueChecker reports missing dependency") {
  auto a = makeMod(QStringLiteral("mod.a"));
  DependencyMod dep;
  dep.packageId = CaseInsensitiveStr(QStringLiteral("mod.b"));
  a->aboutRules.dependencies.append(dep);
  auto storage = makeStorage({{a->packageId, a}});

  DependencyIssueChecker checker;
  CHECK(checker.shouldCheck(*a, storage.ctx()));
  auto issues = checker.check(*a, storage.ctx());
  REQUIRE(issues.size() == 1);
  CHECK(issues[0].shortMessage == QStringLiteral("Missing dependencies"));
  CHECK(issues[0].severity == IssueSeverity::kError);
}

TEST_CASE("DependencyIssueChecker satisfied dependency yields no issues") {
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  DependencyMod dep;
  dep.packageId = CaseInsensitiveStr(QStringLiteral("mod.b"));
  a->aboutRules.dependencies.append(dep);
  auto storage = makeStorage({{a->packageId, a}, {b->packageId, b}});

  DependencyIssueChecker checker;
  CHECK(checker.check(*a, storage.ctx()).isEmpty());
}

TEST_CASE("IncompatibilityIssueChecker no incompatibilities") {
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  auto storage = makeStorage({{a->packageId, a}, {b->packageId, b}});
  IncompatibilityIssueChecker checker;
  CHECK(checker.check(*a, storage.ctx()).isEmpty());
}

TEST_CASE("IncompatibilityIssueChecker self-declared incompatibility") {
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  a->aboutRules.incompatibleWith.add(QStringLiteral("mod.b"));
  auto storage = makeStorage({{a->packageId, a}, {b->packageId, b}});

  IncompatibilityIssueChecker checker;
  auto issues = checker.check(*a, storage.ctx());
  REQUIRE(issues.size() == 1);
  CHECK(issues[0].shortMessage == QStringLiteral("Incompatibilities detected"));
  CHECK(issues[0].severity == IssueSeverity::kError);
}

TEST_CASE("IncompatibilityIssueChecker reverse-declared incompatibility") {
  auto declarer = makeMod(QStringLiteral("mod.declarer"));
  auto target = makeMod(QStringLiteral("mod.target"));
  declarer->aboutRules.incompatibleWith.add(QStringLiteral("mod.target"));
  auto storage = makeStorage(
      {{declarer->packageId, declarer}, {target->packageId, target}});

  IncompatibilityIssueChecker checker;
  auto issues = checker.check(*target, storage.ctx());
  REQUIRE(issues.size() == 1);
  CHECK(issues[0].shortMessage == QStringLiteral("Incompatibilities detected"));
}

TEST_CASE("LoadOrderIssueChecker no violations when order satisfies rules") {
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  a->aboutRules.loadAfter.add(QStringLiteral("mod.b"));
  const QList<PackageId> ordered{PackageId(QStringLiteral("mod.b")),
                                 PackageId(QStringLiteral("mod.a"))};
  auto storage = makeStorage({{a->packageId, a}, {b->packageId, b}}, ordered);

  LoadOrderIssueChecker checker;
  CHECK(checker.check(*a, storage.ctx()).isEmpty());
}

TEST_CASE("LoadOrderIssueChecker load-after violation reported") {
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  a->aboutRules.loadAfter.add(QStringLiteral("mod.b"));
  const QList<PackageId> ordered{PackageId(QStringLiteral("mod.a")),
                                 PackageId(QStringLiteral("mod.b"))};
  auto storage = makeStorage({{a->packageId, a}, {b->packageId, b}}, ordered);

  LoadOrderIssueChecker checker;
  auto issues = checker.check(*a, storage.ctx());
  REQUIRE(issues.size() == 1);
  CHECK(issues[0].shortMessage == QStringLiteral("Load order violated"));
  CHECK(issues[0].severity == IssueSeverity::kWarning);
}

TEST_CASE("LoadOrderIssueChecker load-before violation reported") {
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  a->aboutRules.loadBefore.add(QStringLiteral("mod.b"));
  // A should load before B, but appears after
  const QList<PackageId> ordered{PackageId(QStringLiteral("mod.b")),
                                 PackageId(QStringLiteral("mod.a"))};
  auto storage = makeStorage({{a->packageId, a}, {b->packageId, b}}, ordered);

  LoadOrderIssueChecker checker;
  auto issues = checker.check(*a, storage.ctx());
  REQUIRE(issues.size() == 1);
  CHECK(issues[0].shortMessage == QStringLiteral("Load order violated"));
  CHECK(issues[0].severity == IssueSeverity::kWarning);
}

TEST_CASE("CycleIssueChecker no cycles") {
  auto a = makeMod(QStringLiteral("mod.a"));
  auto storage = makeStorage({{a->packageId, a}});
  CycleIssueChecker checker;
  CHECK(checker.check(*a, storage.ctx()).isEmpty());
}

TEST_CASE("CycleIssueChecker reports cycle membership") {
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  auto storage = makeStorage({{a->packageId, a}, {b->packageId, b}});
  storage.cycles = {
      {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))}};

  CycleIssueChecker checker;
  auto issues = checker.check(*a, storage.ctx());
  REQUIRE(issues.size() == 1);
  CHECK(issues[0].shortMessage == QStringLiteral("Dependency cycle detected"));
  CHECK(issues[0].severity == IssueSeverity::kWarning);
}

TEST_CASE("CycleIssueChecker non-member gets no issue") {
  auto a = makeMod(QStringLiteral("mod.a"));
  auto b = makeMod(QStringLiteral("mod.b"));
  auto c = makeMod(QStringLiteral("mod.c"));
  auto storage =
      makeStorage({{a->packageId, a}, {b->packageId, b}, {c->packageId, c}});
  storage.cycles = {
      {PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))}};

  CycleIssueChecker checker;
  CHECK(checker.check(*c, storage.ctx()).isEmpty());
}

TEST_CASE("GameVersionIssueChecker no versions declared is not checked") {
  auto a = makeMod(QStringLiteral("mod.a"));
  a->supportedVersions.clear();
  auto storage = makeStorage({{a->packageId, a}});

  GameVersionIssueChecker checker;
  CHECK(!checker.shouldCheck(*a, storage.ctx()));
  CHECK(checker.check(*a, storage.ctx()).isEmpty());
}

TEST_CASE("GameVersionIssueChecker matching version yields no issues") {
  auto a = makeMod(QStringLiteral("mod.a"));
  a->supportedVersions = {QStringLiteral("1.5")};
  auto storage = makeStorage({{a->packageId, a}});
  storage.targetVersion = QStringLiteral("1.5");

  GameVersionIssueChecker checker;
  CHECK(checker.check(*a, storage.ctx()).isEmpty());
}

TEST_CASE("GameVersionIssueChecker mismatch reported as warning") {
  auto a = makeMod(QStringLiteral("mod.a"));
  a->supportedVersions = {QStringLiteral("1.4")};
  auto storage = makeStorage({{a->packageId, a}});
  storage.targetVersion = QStringLiteral("1.5");

  GameVersionIssueChecker checker;
  auto issues = checker.check(*a, storage.ctx());
  REQUIRE(issues.size() == 1);
  CHECK(issues[0].shortMessage == QStringLiteral("Version mismatch"));
  CHECK(issues[0].severity == IssueSeverity::kWarning);
}

TEST_CASE("GameVersionIssueChecker suppressed by no_version_warning") {
  auto a = makeMod(QStringLiteral("mod.a"));
  a->supportedVersions = {QStringLiteral("1.4")};
  auto storage = makeStorage({{a->packageId, a}});
  storage.targetVersion = QStringLiteral("1.5");
  storage.noVersionWarning.insert(PackageId(QStringLiteral("mod.a")));

  GameVersionIssueChecker checker;
  CHECK(checker.check(*a, storage.ctx()).isEmpty());
}

TEST_CASE("ReplacementIssueChecker no replacement without published file id") {
  auto a = makeMod(QStringLiteral("mod.a"));
  auto storage = makeStorage({{a->packageId, a}});
  ReplacementIssueChecker checker;
  CHECK(checker.check(*a, storage.ctx()).isEmpty());
}
