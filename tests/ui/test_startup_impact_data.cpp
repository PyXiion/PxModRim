#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ui/panels/startup_impact_data.h"
using namespace px::modrim;
using namespace px::modrim::startup_impact;
namespace {
StartupImpactReport report() {
    StartupImpactReport result;
    result.mods = {
        {"Small", QStringLiteral("a.small"), 1, {{"LoadingProgress.StartupImpact.ApplyPatches", 1}}},
        {"Big", QStringLiteral("b.big"), 4, {{"LoadingProgress.StartupImpact.ModContentPackReloadContentInt.Textures", 3},
                                            {"LoadingProgress.StartupImpact.ApplyPatches", 1}}, {}, 0.5},
        {QString(BASE_GAME_PID), QString(BASE_GAME_PID), 10,
            {{"LoadingProgress.StartupImpact.GlobalTextureAtlasManagerBakeStaticAtlases", 10}}}};
    return result;
}
QVariantMap build(QStringList active = {}, std::optional<QString> selected = {}, double estimate = 0) {
    return build_startup_impact(report(), active, selected, estimate);
}
}
TEST_CASE("Startup impact excludes base and ranks mods") {
    const auto rows = build().value("mods").toList();
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].toMap().value("name").toString() == "Big");
    CHECK(rows[1].toMap().value("name").toString() == "Small");
    CHECK(rows[0].toMap().value("rank").toInt() == 1);
    CHECK(rows[1].toMap().value("rank").toInt() == 2);
    const auto sum = [](const QVariantMap& row) {
        double total = 0;
        for (const auto& segment : row.value("segments").toList()) total += segment.toMap().value("fraction").toDouble();
        return total;
    };
    CHECK(sum(rows[0].toMap()) == Catch::Approx(1));
    CHECK(sum(rows[1].toMap()) == Catch::Approx(0.25));
    CHECK(rows[0].toMap().value("dominant").toString() == "Textures & audio");
    CHECK(rows[0].toMap().value("off_thread").toString() == "500 ms");
}
TEST_CASE("Startup impact normalizes selected and active package IDs") {
    const auto data = build({"B.Big_steam"}, QStringLiteral("A.SMALL"));
    const auto rows = data.value("mods").toList();
    CHECK(rows[0].toMap().value("active").toBool());
    CHECK_FALSE(rows[0].toMap().value("selected").toBool());
    CHECK_FALSE(rows[1].toMap().value("active").toBool());
    CHECK(rows[1].toMap().value("selected").toBool());
    CHECK(data.value("active_count").toInt() == 1);
    CHECK(data.value("selected_key").toString() == "a.small");
}
TEST_CASE("Startup impact summary and estimates") {
    const auto data = build({}, {}, 12);
    CHECK(data.value("estimated").toString().isEmpty());
    CHECK(build({"a.small"}, {}, 12).value("estimated").toString() == "12.00 s");
    CHECK(data.value("last_launch").toString() == "15.00 s");
    const auto summary = data.value("summary").toList();
    REQUIRE(summary.size() == 3);
    double total = 0;
    for (qsizetype i = 0; i < summary.size(); ++i) {
        CHECK(summary[i].toMap().value("parallel").toBool() == (i == 2));
        total += summary[i].toMap().value("fraction").toDouble();
    }
    CHECK(total == Catch::Approx(1));
    CHECK(summary[0].toMap().value("fraction").toDouble() == Catch::Approx(10.0 / 15.5));
}
TEST_CASE("Startup impact phases toggle base contribution") {
    const auto data = build({}, QStringLiteral("b.big"));
    const auto phases = data.value("phases_with_base").toList();
    REQUIRE(phases.size() == 2);
    const auto content = phases[0].toMap();
    CHECK(content.value("label").toString() == "Textures & audio");
    CHECK(phases[1].toMap().value("label").toString() == "Patches");
    CHECK(content.value("value").toString() == "13.00 s");
    CHECK(content.value("share").toString() == "87%");
    const auto contributors = content.value("mods").toList();
    REQUIRE(contributors.size() == 2);
    CHECK(contributors[0].toMap().value("name").toString() == "Base game");
    CHECK(contributors[0].toMap().value("base").toBool());
    CHECK(contributors[1].toMap().value("selected").toBool());
    const auto modsOnly = data.value("phases_mods_only").toList();
    REQUIRE(modsOnly.size() == 2);
    CHECK(modsOnly[0].toMap().value("value").toString() == "3.00 s");
    CHECK(modsOnly[1].toMap().value("value").toString() == "2.00 s");
    for (const auto& phase : modsOnly)
        for (const auto& mod : phase.toMap().value("mods").toList()) CHECK_FALSE(mod.toMap().value("base").toBool());
}
TEST_CASE("Startup impact duration formatting") {
    CHECK(format_duration(0.0004) == "0 ms");
    CHECK(format_duration(0.25) == "250 ms");
    CHECK(format_duration(59.5) == "59.50 s");
    CHECK(format_duration(60) == "1m 00s");
    CHECK(format_duration(268.7) == "4m 29s");
}
TEST_CASE("Startup impact tooltip combines metrics sharing labels") {
    auto data = report();
    data.mods[0].metrics = {{"ApplyPatches|Foo -> a", 1}, {"ApplyPatches|Foo -> b", 2}};
    const auto rows = build_startup_impact(data, {}, {}, 0).value("mods").toList();
    const auto tooltip = rows[1].toMap().value("phases").toList()[0].toMap().value("tooltip").toString();
    CHECK(tooltip.count('\n') == 0);
    CHECK(tooltip.endsWith("3.00 s"));
}
