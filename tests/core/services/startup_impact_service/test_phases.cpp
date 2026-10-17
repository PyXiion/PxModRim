#include <QMap>
#include <QString>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/services/startup_impact_service/labels.hpp"
#include "core/services/startup_impact_service/phases.hpp"

using namespace px::modrim::startup_impact;
using Catch::Matchers::WithinAbs;

namespace {

const QString kPrefix = QStringLiteral("LoadingProgress.StartupImpact.");

}  // namespace

TEST_CASE("metric_phase assigns each metric key to its phase") {
  struct Case {
    const char* key;
    Phase phase;
  };
  const Case cases[] = {
      {"ModContentPackReloadContentInt.Textures", Phase::kContent},
      {"GlobalTextureAtlasManagerBakeStaticAtlases", Phase::kContent},
      {"ModConstructor", Phase::kModConstructors},
      {"StaticConstructorOnStartupUtilityCallAll", Phase::kStaticConstructors},
      {"ApplyPatches", Phase::kPatches},
      {"ErrorCheckPatches", Phase::kPatches},
      {"LoadDefs", Phase::kDefs},
      {"ResolveAllWantedCrossReferences.NonImplied", Phase::kDefs},
      {"ExecuteToExecuteWhenFinished|HugsLib.Utils.HarmonyUtility",
       Phase::kDeferred},
      {"LanguageDatabaseInitAllMetadata", Phase::kOther},
      {"SomethingNew", Phase::kOther},
  };
  for (const Case& c : cases) {
    INFO(c.key);
    CHECK(metricPhase(kPrefix + QString::fromLatin1(c.key)) == c.phase);
  }
}

TEST_CASE("metric_phase also accepts keys without the prefix") {
  CHECK(metricPhase(QStringLiteral("LoadDefs")) == Phase::kDefs);
}

TEST_CASE("phase_totals sums per phase and zero-fills the rest") {
  const QMap<QString, double> metrics{
      {kPrefix + QStringLiteral("ApplyPatches"), 1.5},
      {kPrefix + QStringLiteral("LoadPatches"), 0.5},
      {kPrefix + QStringLiteral("LoadDefs"), 2.0},
  };
  const PhaseTotals totals = phaseTotals(metrics);
  CHECK_THAT(totals[phaseIndex(Phase::kPatches)], WithinAbs(2.0, 1e-9));
  CHECK_THAT(totals[phaseIndex(Phase::kDefs)], WithinAbs(2.0, 1e-9));
  CHECK(totals[phaseIndex(Phase::kContent)] == 0.0);
  CHECK(totals.size() == kPhaseCount);
}

TEST_CASE("every phase has a label") {
  for (std::size_t i = 0; i < kPhaseCount; ++i) {
    CHECK_FALSE(phaseLabel(static_cast<Phase>(i)).isEmpty());
  }
  CHECK(phaseLabel(Phase::kDefs) == QStringLiteral("Defs & XML"));
}

TEST_CASE("metric_label names a deferred task's target") {
  const QString key = kPrefix + QStringLiteral(
                                    "ExecuteToExecuteWhenFinished|RimWorld."
                                    "AbilityDef -> Void <PostLoad>b__88_0()");
  CHECK(metricLabel(key) ==
        QStringLiteral(
            "Running delayed initialization task (RimWorld.AbilityDef)"));
}

TEST_CASE(
    "metric_label falls back to the last key segment, then the key itself") {
  CHECK(metricLabel(kPrefix + QStringLiteral(
                                  "ModContentPackReloadContentInt.Textures")) ==
        QStringLiteral("Loading textures"));
  CHECK(metricLabel(kPrefix + QStringLiteral("Some.Unknown.LoadDefs")) ==
        QStringLiteral("Loading defs"));
  CHECK(metricLabel(kPrefix + QStringLiteral("Totally.New")) ==
        QStringLiteral("Totally.New"));
  CHECK(metricLabel(QStringLiteral("ModConstructor")) ==
        QStringLiteral("Running mod constructor"));
}
