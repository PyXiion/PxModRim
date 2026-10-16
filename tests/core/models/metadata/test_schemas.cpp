#include <catch2/catch_test_macros.hpp>

#include <string>

#include <glaze/glaze.hpp>

#include "core/models/metadata/structures.h"

using namespace px::modrim;

TEST_CASE("CaseInsensitiveStr serializes as a lowercased JSON string") {
    const CaseInsensitiveStr pid(QStringLiteral("Ludeon.RimWorld"));
    const auto written = glz::write_json(pid);
    REQUIRE(written.has_value());
    CHECK(*written == R"("ludeon.rimworld")");

    CaseInsensitiveStr parsed;
    REQUIRE_FALSE(glz::read_json(parsed, R"("Test.Mod")"));
    CHECK(parsed == CaseInsensitiveStr(QStringLiteral("test.mod")));
}

TEST_CASE("community rules schema accepts single-value and list comments") {
    const std::string json = R"({
        "timestamp": 42,
        "rules": {
            "pkg.a": {
                "loadAfter": {
                    "other": {"name": "Other", "comment": ["first", "second"]}
                },
                "loadTop": {"value": true, "comment": "top"}
            }
        }
    })";

    ExternalRulesSchema schema;
    const auto error = glz::read_json(schema, json);
    REQUIRE_FALSE(error);
    CHECK(schema.timestamp == 42);

    const ExternalRule& rule = schema.rules.at("pkg.a");
    CHECK(rule.loadTop.value);
    CHECK(std::get<std::string>(rule.loadTop.comment) == "top");

    const SubExternalRule& after = rule.loadAfter.at("other");
    CHECK(std::get<std::string>(after.name) == "Other");
    CHECK(std::get<std::vector<std::string>>(after.comment) ==
          std::vector<std::string>{"first", "second"});
    CHECK(rule.loadBottom.value == false);
}

TEST_CASE("steam workshop database accepts null, list, and object unions") {
    const std::string json = R"({
        "version": 3,
        "database": {
            "123": {
                "packageId": "author.mod",
                "gameVersions": null,
                "authors": "Solo Author",
                "dependencies": {
                    "dep.list": ["alt.a"],
                    "dep.object": {"name": "Dep", "url": "https://example.test"}
                },
                "tags": [{"key": "value"}],
                "blacklist": {"value": true, "comment": "outdated"}
            }
        }
    })";

    SteamDbSchema schema;
    const auto error = glz::read_json(schema, json);
    REQUIRE_FALSE(error);
    CHECK(schema.version == 3);

    const SteamDbEntry& entry = schema.database.at("123");
    CHECK(entry.packageId == "author.mod");
    CHECK(std::holds_alternative<std::nullptr_t>(entry.gameVersions));
    CHECK(std::get<std::string>(entry.authors) == "Solo Author");
    CHECK(std::get<std::vector<std::string>>(entry.dependencies.at("dep.list")) ==
          std::vector<std::string>{"alt.a"});
    const auto& object = std::get<SteamDbEntryDependency>(entry.dependencies.at("dep.object"));
    CHECK(object.name == "Dep");
    CHECK(object.url == "https://example.test");
    REQUIRE(entry.tags.size() == 1);
    CHECK(entry.tags.front().at("key") == "value");
    CHECK(entry.blacklist.value);
    CHECK(entry.blacklist.comment == "outdated");
}
