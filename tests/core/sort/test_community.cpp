#include <QString>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include "../test_support.hpp"
#include "core/sort/community.hpp"

using px::modrim::sort::loadCommunityRules;
using px::modrim::sort::PackageId;

TEST_CASE("a missing community rules file means no rules") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());

  const auto rules =
      loadCommunityRules(dir.filePath(QStringLiteral("communityRules.json")));

  REQUIRE(rules.has_value());
  CHECK(rules->isEmpty());
}

TEST_CASE("community rules are keyed by lowercased package id") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("communityRules.json"));
  REQUIRE(test_support::writeBytes(path, R"({
        "timestamp": 1,
        "rules": {
            "Some.Mod": {
                "loadAfter": {"Base.Mod": {"name": "Base"}},
                "loadBefore": {"Late.Mod": {}},
                "loadTop": {"value": true},
                "loadBottom": {"value": false}
            }
        }
    })"));

  const auto loaded = loadCommunityRules(path);

  REQUIRE(loaded.has_value());
  REQUIRE(loaded->contains(PackageId(QStringLiteral("some.mod"))));
  const auto& rule = loaded->value(PackageId(QStringLiteral("some.mod")));
  CHECK(rule.packageId == PackageId(QStringLiteral("some.mod")));
  CHECK(rule.loadAfter.contains(QStringLiteral("base.mod")));
  CHECK(rule.loadBefore.contains(QStringLiteral("late.mod")));
  CHECK(rule.loadFirst);
  CHECK_FALSE(rule.loadLast);
}

TEST_CASE("a malformed community rules file is an error") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("communityRules.json"));
  REQUIRE(test_support::writeBytes(path, R"({"rules": [1, 2)"));

  const auto loaded = loadCommunityRules(path);

  CHECK_FALSE(loaded.has_value());
}
