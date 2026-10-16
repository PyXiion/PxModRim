#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QMap>
#include <QString>

#include "core/services/startup_impact_service/labels.h"
#include "core/services/startup_impact_service/phases.h"

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
        {"ModContentPackReloadContentInt.Textures", Phase::Content},
        {"GlobalTextureAtlasManagerBakeStaticAtlases", Phase::Content},
        {"ModConstructor", Phase::ModConstructors},
        {"StaticConstructorOnStartupUtilityCallAll", Phase::StaticConstructors},
        {"ApplyPatches", Phase::Patches},
        {"ErrorCheckPatches", Phase::Patches},
        {"LoadDefs", Phase::Defs},
        {"ResolveAllWantedCrossReferences.NonImplied", Phase::Defs},
        {"ExecuteToExecuteWhenFinished|HugsLib.Utils.HarmonyUtility", Phase::Deferred},
        {"LanguageDatabaseInitAllMetadata", Phase::Other},
        {"SomethingNew", Phase::Other},
    };
    for (const Case& c : cases) {
        INFO(c.key);
        CHECK(metric_phase(kPrefix + QString::fromLatin1(c.key)) == c.phase);
    }
}

TEST_CASE("metric_phase also accepts keys without the prefix") {
    CHECK(metric_phase(QStringLiteral("LoadDefs")) == Phase::Defs);
}

TEST_CASE("phase_totals sums per phase and zero-fills the rest") {
    const QMap<QString, double> metrics{
        {kPrefix + QStringLiteral("ApplyPatches"), 1.5},
        {kPrefix + QStringLiteral("LoadPatches"), 0.5},
        {kPrefix + QStringLiteral("LoadDefs"), 2.0},
    };
    const PhaseTotals totals = phase_totals(metrics);
    CHECK_THAT(totals[phase_index(Phase::Patches)], WithinAbs(2.0, 1e-9));
    CHECK_THAT(totals[phase_index(Phase::Defs)], WithinAbs(2.0, 1e-9));
    CHECK(totals[phase_index(Phase::Content)] == 0.0);
    CHECK(totals.size() == PHASE_COUNT);
}

TEST_CASE("every phase has a label") {
    for (std::size_t i = 0; i < PHASE_COUNT; ++i) {
        CHECK_FALSE(phase_label(static_cast<Phase>(i)).isEmpty());
    }
    CHECK(phase_label(Phase::Defs) == QStringLiteral("Defs & XML"));
}

TEST_CASE("metric_label names a deferred task's target") {
    const QString key =
        kPrefix +
        QStringLiteral("ExecuteToExecuteWhenFinished|RimWorld.AbilityDef -> Void <PostLoad>b__88_0()");
    CHECK(metric_label(key) ==
          QStringLiteral("Running delayed initialization task (RimWorld.AbilityDef)"));
}

TEST_CASE("metric_label falls back to the last key segment, then the key itself") {
    CHECK(metric_label(kPrefix + QStringLiteral("ModContentPackReloadContentInt.Textures")) ==
          QStringLiteral("Loading textures"));
    CHECK(metric_label(kPrefix + QStringLiteral("Some.Unknown.LoadDefs")) ==
          QStringLiteral("Loading defs"));
    CHECK(metric_label(kPrefix + QStringLiteral("Totally.New")) == QStringLiteral("Totally.New"));
    CHECK(metric_label(QStringLiteral("ModConstructor")) == QStringLiteral("Running mod constructor"));
}
