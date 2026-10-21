#include <QDir>
#include <QString>
#include <QTemporaryDir>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../../test_support.hpp"
#include "core/services/startup_impact_service/models.hpp"
#include "core/services/startup_impact_service/parser.hpp"

using namespace px::modrim::startup_impact;
using Catch::Matchers::WithinAbs;
using test_support::writeBytes;

namespace {

constexpr double kEps = 1e-9;

const char* kSampleJson = R"({
  "timestamp": "2026-07-16T14:30:45.123Z",
  "loadingTime": 42500.5,
  "metrics": {
    "LoadModXml": 10200.0,
    "ResolveReferences": 1500.0
  },
  "totalImpact": 0.0,
  "offThreadMetrics": {},
  "offThreadTotalImpact": 0.0,
  "mods": [
    {
      "modName": "Harmony",
      "modPackageId": "brrainz.harmony",
      "metrics": {
        "ModConstructor": 1000.0
      },
      "totalImpact": 1230.5,
      "offThreadMetrics": {
        "LoadPatches": 300.0
      },
      "offThreadTotalImpact": 300.0
    },
    {
      "modName": "Some Mod",
      "modPackageId": "author.somemod",
      "metrics": {
        "LoadModXml": 4500.0
      },
      "totalImpact": 4500.0,
      "offThreadMetrics": {},
      "offThreadTotalImpact": 0.0
    },
    {
      "modName": "Fast Mod",
      "modPackageId": "author.fast",
      "metrics": {},
      "totalImpact": 50.0,
      "offThreadMetrics": {},
      "offThreadTotalImpact": 0.0
    },
    {
      "modName": "No Package ID Mod",
      "totalImpact": 200.0,
      "metrics": {},
      "offThreadMetrics": {},
      "offThreadTotalImpact": 0.0
    }
  ]
}
)";

// Writes `json` to a file in `dir` and parses it.
std::optional<StartupImpactReport> parseText(const QTemporaryDir& dir,
                                             const QString& name,
                                             const char* json) {
  const QString path = QDir(dir.path()).filePath(name);
  REQUIRE(writeBytes(path, json));
  return parseStartupImpact(path);
}

}  // namespace

TEST_CASE("normalize_package_id lowercases and strips the steam suffix") {
  CHECK(normalizePackageId(QStringLiteral("Author.Mod")) ==
        QStringLiteral("author.mod"));
  CHECK(normalizePackageId(QStringLiteral("author.mod_steam")) ==
        QStringLiteral("author.mod"));
  CHECK(normalizePackageId(QStringLiteral("author.mod")) ==
        QStringLiteral("author.mod"));
}

TEST_CASE("get_startup_impact_path sits beside the config folder") {
  CHECK(getStartupImpactPath(QStringLiteral("/home/user/Config")) ==
        QStringLiteral("/home/user/StartupImpactData.json"));
  CHECK(getStartupImpactPath(QStringLiteral("/home/user/Config/")) ==
        QStringLiteral("/home/user/StartupImpactData.json"));
  CHECK(getStartupImpactPath(QStringLiteral("/Config")) ==
        QStringLiteral("/StartupImpactData.json"));
}

TEST_CASE("parse_startup_impact reads the full report") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const auto report =
      parseText(dir, QStringLiteral("StartupImpactData.json"), kSampleJson);
  REQUIRE(report.has_value());
  CHECK(report->loadingTimeS == Catch::Approx(42.5005));
  CHECK(report->timestamp == QStringLiteral("2026-07-16T14:30:45.123Z"));
  REQUIRE(report->metrics.size() == 2);
  CHECK(report->metrics.value(QStringLiteral("LoadModXml")) ==
        Catch::Approx(10.2));
  CHECK(report->metrics.value(QStringLiteral("ResolveReferences")) ==
        Catch::Approx(1.5));
  CHECK(report->mods.size() == 4);
}

TEST_CASE("base game time is the sum of the base-game metrics") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const auto report =
      parseText(dir, QStringLiteral("StartupImpactData.json"), kSampleJson);
  REQUIRE(report.has_value());
  CHECK_THAT(report->baseGameTime(), WithinAbs(11.7, kEps));
}

TEST_CASE("parse_startup_impact reads mod values") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const auto report =
      parseText(dir, QStringLiteral("StartupImpactData.json"), kSampleJson);
  REQUIRE(report.has_value());

  const StartupImpactMod& harmony = report->mods[0];
  CHECK(harmony.modName == QStringLiteral("Harmony"));
  CHECK(harmony.packageId == QStringLiteral("brrainz.harmony"));
  CHECK(harmony.totalImpactS == Catch::Approx(1.2305));
  CHECK(harmony.metrics.value(QStringLiteral("ModConstructor")) ==
        Catch::Approx(1.0));

  const StartupImpactMod& someMod = report->mods[1];
  CHECK(someMod.modName == QStringLiteral("Some Mod"));
  CHECK(someMod.packageId == QStringLiteral("author.somemod"));
  CHECK(someMod.totalImpactS == Catch::Approx(4.5));
  CHECK(someMod.metrics.value(QStringLiteral("LoadModXml")) ==
        Catch::Approx(4.5));

  const StartupImpactMod& fast = report->mods[2];
  CHECK(fast.modName == QStringLiteral("Fast Mod"));
  CHECK(fast.packageId == QStringLiteral("author.fast"));
  CHECK(fast.totalImpactS == Catch::Approx(0.05));
  CHECK(fast.metrics.isEmpty());
}

TEST_CASE("a mod without a package id has none after parsing") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const auto report =
      parseText(dir, QStringLiteral("StartupImpactData.json"), kSampleJson);
  REQUIRE(report.has_value());

  const StartupImpactMod& noPid = report->mods[3];
  CHECK(noPid.modName == QStringLiteral("No Package ID Mod"));
  CHECK_FALSE(noPid.packageId.has_value());
  CHECK(noPid.totalImpactS == Catch::Approx(0.2));
  CHECK(noPid.metrics.isEmpty());
}

TEST_CASE("parse_startup_impact returns nullopt for a missing file") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  CHECK_FALSE(
      parseStartupImpact(QDir(dir.path()).filePath(QStringLiteral("nope.json")))
          .has_value());
}

TEST_CASE("parse_startup_impact reads an empty mod list") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const auto report = parseText(dir, QStringLiteral("empty_mods.json"),
                                R"({"loadingTime": 1000.0, "mods": []})");
  REQUIRE(report.has_value());
  CHECK(report->loadingTimeS == Catch::Approx(1.0));
  CHECK(report->mods.isEmpty());
}

TEST_CASE("parse_startup_impact rejects a top-level array") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  CHECK_FALSE(
      parseText(dir, QStringLiteral("not_a_dict.json"), "[]").has_value());
}

TEST_CASE("parse_startup_impact rejects malformed JSON") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  CHECK_FALSE(
      parseText(dir, QStringLiteral("bad.json"), "{invalid").has_value());
}

TEST_CASE("parse_startup_impact accepts an object without known keys") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const auto report = parseText(dir, QStringLiteral("invalid.json"),
                                R"({"wrongKey": "value"})");
  REQUIRE(report.has_value());
  CHECK(report->mods.isEmpty());
}

TEST_CASE("parse_startup_impact reads off-thread mod fields") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const auto report = parseText(dir, QStringLiteral("offthread.json"), R"({
        "loadingTime": 10000.0,
        "mods": [
            {
                "modName": "Test Mod",
                "modPackageId": "author.test",
                "totalImpact": 2000.0,
                "metrics": {"A": 1000.0, "B": 1000.0},
                "offThreadMetrics": {"C": 500.0},
                "offThreadTotalImpact": 500.0
            }
        ]
    })");
  REQUIRE(report.has_value());
  const StartupImpactMod& mod = report->mods[0];
  REQUIRE(mod.offThreadMetrics.size() == 1);
  CHECK(mod.offThreadMetrics.value(QStringLiteral("C")) == Catch::Approx(0.5));
  CHECK(mod.offThreadTotalImpactS == Catch::Approx(0.5));
}

TEST_CASE("find matches by normalized package id") {
  StartupImpactReport report;
  StartupImpactMod m1;
  m1.modName = QStringLiteral("Harmony");
  m1.packageId = QStringLiteral("brrainz.harmony");
  report.mods.append(std::move(m1));
  StartupImpactMod m2;
  m2.modName = QStringLiteral("Other");
  m2.packageId = QStringLiteral("other.mod");
  report.mods.append(std::move(m2));

  const StartupImpactMod* byId =
      report.find(QStringLiteral("Brrainz.Harmony_steam"));
  REQUIRE(byId != nullptr);
  CHECK(byId->modName == QStringLiteral("Harmony"));

  CHECK(report.find(QStringLiteral("missing.mod")) == nullptr);
  CHECK(report.find(QString()) == nullptr);
}
