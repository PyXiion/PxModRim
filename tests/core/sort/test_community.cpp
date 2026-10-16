#include <catch2/catch_test_macros.hpp>

#include <QString>
#include <QTemporaryDir>

#include "core/sort/community.h"
#include "../test_support.h"

using px::modrim::sort::load_community_rules;
using px::modrim::sort::PackageId;

TEST_CASE("a missing community rules file means no rules") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const auto rules = load_community_rules(dir.filePath(QStringLiteral("communityRules.json")));

    REQUIRE(rules.has_value());
    CHECK(rules->isEmpty());
}

TEST_CASE("community rules are keyed by lowercased package id") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("communityRules.json"));
    REQUIRE(test_support::write_bytes(path, R"({
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

    const auto loaded = load_community_rules(path);

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->contains(PackageId(QStringLiteral("some.mod"))));
    const auto& rule = loaded->value(PackageId(QStringLiteral("some.mod")));
    CHECK(rule.package_id == PackageId(QStringLiteral("some.mod")));
    CHECK(rule.load_after.contains(QStringLiteral("base.mod")));
    CHECK(rule.load_before.contains(QStringLiteral("late.mod")));
    CHECK(rule.load_first);
    CHECK_FALSE(rule.load_last);
    CHECK(rule.incompatible_with.empty());
}

TEST_CASE("a malformed community rules file is an error") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("communityRules.json"));
    REQUIRE(test_support::write_bytes(path, R"({"rules": [1, 2)"));

    const auto loaded = load_community_rules(path);

    CHECK_FALSE(loaded.has_value());
}
