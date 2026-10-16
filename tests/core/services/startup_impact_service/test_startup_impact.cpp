#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QDir>
#include <QString>
#include <QTemporaryDir>

#include "core/services/startup_impact_service/models.h"
#include "core/services/startup_impact_service/parser.h"
#include "../../test_support.h"

using namespace px::modrim::startup_impact;
using Catch::Matchers::WithinAbs;
using test_support::write_bytes;

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
std::optional<StartupImpactReport> parse_text(const QTemporaryDir& dir, const QString& name,
                                              const char* json) {
    const QString path = QDir(dir.path()).filePath(name);
    REQUIRE(write_bytes(path, json));
    return parse_startup_impact(path);
}

}  // namespace

TEST_CASE("normalize_package_id lowercases and strips the steam suffix") {
    CHECK(normalize_package_id(QStringLiteral("Author.Mod")) == QStringLiteral("author.mod"));
    CHECK(normalize_package_id(QStringLiteral("author.mod_steam")) ==
          QStringLiteral("author.mod"));
    CHECK(normalize_package_id(QStringLiteral("author.mod")) == QStringLiteral("author.mod"));
}

TEST_CASE("format_impact rounds seconds to whole milliseconds") {
    CHECK(format_impact(1.234) == QStringLiteral("1234ms"));
    CHECK(format_impact(0.05) == QStringLiteral("50ms"));
    CHECK(format_impact(0.0) == QStringLiteral("0ms"));
}

TEST_CASE("format_impact rounds half to even like Python's round") {
    CHECK(format_impact(0.0005) == QStringLiteral("0ms"));
    CHECK(format_impact(0.0015) == QStringLiteral("2ms"));
    CHECK(format_impact(0.0025) == QStringLiteral("2ms"));
}

TEST_CASE("get_startup_impact_path sits beside the config folder") {
    CHECK(get_startup_impact_path(QStringLiteral("/home/user/Config")) ==
          QStringLiteral("/home/user/StartupImpactData.json"));
    CHECK(get_startup_impact_path(QStringLiteral("/home/user/Config/")) ==
          QStringLiteral("/home/user/StartupImpactData.json"));
    CHECK(get_startup_impact_path(QStringLiteral("/Config")) ==
          QStringLiteral("/StartupImpactData.json"));
}

TEST_CASE("parse_startup_impact reads the full report") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto report = parse_text(dir, QStringLiteral("StartupImpactData.json"), kSampleJson);
    REQUIRE(report.has_value());
    CHECK(report->loading_time_s == Catch::Approx(42.5005));
    CHECK(report->timestamp == QStringLiteral("2026-07-16T14:30:45.123Z"));
    REQUIRE(report->metrics.size() == 2);
    CHECK(report->metrics.value(QStringLiteral("LoadModXml")) == Catch::Approx(10.2));
    CHECK(report->metrics.value(QStringLiteral("ResolveReferences")) == Catch::Approx(1.5));
    CHECK(report->total_impact == 0.0);
    CHECK(report->mods.size() == 4);
}

TEST_CASE("base game time is the sum of the base-game metrics") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto report = parse_text(dir, QStringLiteral("StartupImpactData.json"), kSampleJson);
    REQUIRE(report.has_value());
    // Not loading time minus mod impacts: loading time includes unaccounted time.
    CHECK_THAT(report->base_game_time(), WithinAbs(11.7, kEps));
    CHECK(std::abs(report->loading_time_s - report->total_time()) > 1.0);
}

TEST_CASE("mods_total_time includes off-thread impact") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto report = parse_text(dir, QStringLiteral("StartupImpactData.json"), kSampleJson);
    REQUIRE(report.has_value());
    const StartupImpactMod& harmony = report->mods[0];
    CHECK(harmony.total_impact_s == Catch::Approx(1.2305));
    CHECK(harmony.off_thread_total_impact_s == Catch::Approx(0.3));
    // total_time counts on-thread impact only; mods_total_time adds off-thread impact.
    CHECK_THAT(report->total_time(), WithinAbs(1.2305 + 4.5 + 0.05 + 0.2, kEps));
    CHECK_THAT(report->off_thread_total_time(), WithinAbs(0.3, kEps));
    CHECK_THAT(report->mods_total_time(), WithinAbs(report->total_time() + 0.3, kEps));
}

TEST_CASE("parse_startup_impact reads mod values") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto report = parse_text(dir, QStringLiteral("StartupImpactData.json"), kSampleJson);
    REQUIRE(report.has_value());

    const StartupImpactMod& harmony = report->mods[0];
    CHECK(harmony.mod_name == QStringLiteral("Harmony"));
    CHECK(harmony.package_id == QStringLiteral("brrainz.harmony"));
    CHECK(harmony.total_impact_s == Catch::Approx(1.2305));
    CHECK(harmony.metrics.value(QStringLiteral("ModConstructor")) == Catch::Approx(1.0));

    const StartupImpactMod& some_mod = report->mods[1];
    CHECK(some_mod.mod_name == QStringLiteral("Some Mod"));
    CHECK(some_mod.package_id == QStringLiteral("author.somemod"));
    CHECK(some_mod.total_impact_s == Catch::Approx(4.5));
    CHECK(some_mod.metrics.value(QStringLiteral("LoadModXml")) == Catch::Approx(4.5));

    const StartupImpactMod& fast = report->mods[2];
    CHECK(fast.mod_name == QStringLiteral("Fast Mod"));
    CHECK(fast.package_id == QStringLiteral("author.fast"));
    CHECK(fast.total_impact_s == Catch::Approx(0.05));
    CHECK(fast.metrics.isEmpty());
}

TEST_CASE("a mod without a package id has none after parsing") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto report = parse_text(dir, QStringLiteral("StartupImpactData.json"), kSampleJson);
    REQUIRE(report.has_value());

    const StartupImpactMod& no_pid = report->mods[3];
    CHECK(no_pid.mod_name == QStringLiteral("No Package ID Mod"));
    CHECK_FALSE(no_pid.package_id.has_value());
    CHECK(no_pid.total_impact_s == Catch::Approx(0.2));
    CHECK(no_pid.metrics.isEmpty());
}

TEST_CASE("parse_startup_impact returns nullopt for a missing file") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    CHECK_FALSE(parse_startup_impact(QDir(dir.path()).filePath(QStringLiteral("nope.json")))
                    .has_value());
}

TEST_CASE("parse_startup_impact reads an empty mod list") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto report = parse_text(dir, QStringLiteral("empty_mods.json"),
                                   R"({"loadingTime": 1000.0, "mods": []})");
    REQUIRE(report.has_value());
    CHECK(report->loading_time_s == Catch::Approx(1.0));
    CHECK(report->mods.isEmpty());
}

TEST_CASE("parse_startup_impact rejects a top-level array") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    CHECK_FALSE(parse_text(dir, QStringLiteral("not_a_dict.json"), "[]").has_value());
}

TEST_CASE("parse_startup_impact rejects malformed JSON") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    CHECK_FALSE(parse_text(dir, QStringLiteral("bad.json"), "{invalid").has_value());
}

TEST_CASE("parse_startup_impact accepts an object without known keys") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto report = parse_text(dir, QStringLiteral("invalid.json"),
                                   R"({"wrongKey": "value"})");
    REQUIRE(report.has_value());
    CHECK(report->mods.isEmpty());
}

TEST_CASE("total_time sums on-thread mod impact") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto report = parse_text(dir, QStringLiteral("total.json"), R"({
        "loadingTime": 50000.0,
        "mods": [
            {"modName": "A", "modPackageId": "a.a",
             "totalImpact": 2000.0, "metrics": {},
             "offThreadMetrics": {}, "offThreadTotalImpact": 0.0},
            {"modName": "B", "modPackageId": "b.b",
             "totalImpact": 3000.0, "metrics": {},
             "offThreadMetrics": {}, "offThreadTotalImpact": 0.0}
        ]
    })");
    REQUIRE(report.has_value());
    CHECK(report->total_time() == Catch::Approx(5.0));
}

TEST_CASE("total_time of an empty report is zero") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto report = parse_text(dir, QStringLiteral("total_empty.json"),
                                   R"({"loadingTime": 1000.0, "mods": []})");
    REQUIRE(report.has_value());
    CHECK(report->total_time() == 0.0);
}

TEST_CASE("parse_startup_impact reads off-thread mod fields") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto report = parse_text(dir, QStringLiteral("offthread.json"), R"({
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
    REQUIRE(mod.off_thread_metrics.size() == 1);
    CHECK(mod.off_thread_metrics.value(QStringLiteral("C")) == Catch::Approx(0.5));
    CHECK(mod.off_thread_total_impact_s == Catch::Approx(0.5));
}

TEST_CASE("find matches by package id, then by mod name") {
    StartupImpactReport report;
    StartupImpactMod m1;
    m1.mod_name = QStringLiteral("Harmony");
    m1.package_id = QStringLiteral("brrainz.harmony");
    report.mods.append(std::move(m1));
    StartupImpactMod m2;
    m2.mod_name = QStringLiteral("Other");
    m2.package_id = QStringLiteral("other.mod");
    report.mods.append(std::move(m2));

    const StartupImpactMod* by_id = report.find(QStringLiteral("Brrainz.Harmony_steam"), {});
    REQUIRE(by_id != nullptr);
    CHECK(by_id->mod_name == QStringLiteral("Harmony"));

    const StartupImpactMod* by_name = report.find({}, QStringLiteral("OTHER"));
    REQUIRE(by_name != nullptr);
    CHECK(by_name->package_id == QStringLiteral("other.mod"));

    CHECK(report.find(QStringLiteral("missing.mod"), QStringLiteral("Missing")) == nullptr);
    CHECK(report.find(QString(), QString()) == nullptr);
}
