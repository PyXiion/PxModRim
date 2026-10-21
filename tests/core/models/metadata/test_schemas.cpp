#include <string>

#include <catch2/catch_test_macros.hpp>
#include <glaze/glaze.hpp>

#include "core/models/metadata/structures.hpp"

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
