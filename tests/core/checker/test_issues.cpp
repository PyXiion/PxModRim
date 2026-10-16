#include <catch2/catch_test_macros.hpp>

#include <memory>

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include "core/checker/graph.h"
#include "core/checker/issues.h"
#include "core/checker/models.h"
#include "core/models/metadata/structures.h"
#include "core/sort/config.h"
#include "core/sort/models.h"

using namespace px::modrim;
using namespace px::modrim::checker;

namespace {

std::shared_ptr<AboutXmlMod> make_mod(const QString& package_id) {
    auto m = std::make_shared<AboutXmlMod>();
    m->package_id = CaseInsensitiveStr(package_id);
    m->name = package_id;
    return m;
}

sort::SortSettings default_settings() {
    sort::SortSettings s;
    s.use_community_rules = false;
    s.use_alternative_package_ids = true;
    s.check_missing_dependencies = true;
    return s;
}

// Holds the storage referenced by a CheckContext so tests can construct one simply.
struct ContextStorage {
    QHash<PackageId, std::shared_ptr<AboutXmlMod>> active_mods;
    QList<PackageId> ordered_pids;
    QHash<PackageId, int> pid_to_index;
    ConstraintGraph graph;
    sort::SortSettings settings = default_settings();
    QString target_version = QStringLiteral("1.5");
    QSet<PackageId> no_version_warning;
    QHash<QString, ReplacementInfo> use_this_instead;
    QList<QList<PackageId>> cycles;

    CheckContext ctx() const {
        return CheckContext{
            .active_mods = active_mods,
            .ordered_pids = ordered_pids,
            .pid_to_index = pid_to_index,
            .graph = graph,
            .settings = settings,
            .target_version = target_version,
            .no_version_warning = no_version_warning,
            .use_this_instead = use_this_instead,
            .cycles = cycles,
        };
    }
};

ContextStorage make_storage(QHash<PackageId, std::shared_ptr<AboutXmlMod>> mods,
                            std::optional<QList<PackageId>> ordered = std::nullopt,
                            const sort::SortSettings* settings = nullptr,
                            const sort::CommunityRules* community_rules = nullptr) {
    ContextStorage storage;
    storage.active_mods = std::move(mods);
    storage.ordered_pids = ordered ? *ordered : storage.active_mods.keys();
    if (settings != nullptr) {
        storage.settings = *settings;
    }
    for (qsizetype i = 0; i < storage.ordered_pids.size(); ++i) {
        storage.pid_to_index.insert(storage.ordered_pids[i], static_cast<int>(i));
    }
    storage.graph.build(storage.active_mods, storage.ordered_pids, storage.settings, community_rules);
    return storage;
}

}  // namespace

TEST_CASE("DependencyIssueChecker no issues when mod has no dependencies") {
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    auto storage = make_storage({{a->package_id, a}, {b->package_id, b}});
    DependencyIssueChecker checker;
    CHECK(!checker.should_check(*a, storage.ctx()));
    CHECK(checker.check(*a, storage.ctx()).isEmpty());
}

TEST_CASE("DependencyIssueChecker reports missing dependency") {
    auto a = make_mod(QStringLiteral("mod.a"));
    DependencyMod dep;
    dep.package_id = CaseInsensitiveStr(QStringLiteral("mod.b"));
    a->about_rules.dependencies.append(dep);
    auto storage = make_storage({{a->package_id, a}});

    DependencyIssueChecker checker;
    CHECK(checker.should_check(*a, storage.ctx()));
    auto issues = checker.check(*a, storage.ctx());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].category == QStringLiteral("missing_dependency"));
    CHECK(issues[0].severity == IssueSeverity::Error);
}

TEST_CASE("DependencyIssueChecker satisfied dependency yields no issues") {
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    DependencyMod dep;
    dep.package_id = CaseInsensitiveStr(QStringLiteral("mod.b"));
    a->about_rules.dependencies.append(dep);
    auto storage = make_storage({{a->package_id, a}, {b->package_id, b}});

    DependencyIssueChecker checker;
    CHECK(checker.check(*a, storage.ctx()).isEmpty());
}

TEST_CASE("IncompatibilityIssueChecker no incompatibilities") {
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    auto storage = make_storage({{a->package_id, a}, {b->package_id, b}});
    IncompatibilityIssueChecker checker;
    CHECK(checker.check(*a, storage.ctx()).isEmpty());
}

TEST_CASE("IncompatibilityIssueChecker self-declared incompatibility") {
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    a->about_rules.incompatible_with.add(QStringLiteral("mod.b"));
    auto storage = make_storage({{a->package_id, a}, {b->package_id, b}});

    IncompatibilityIssueChecker checker;
    auto issues = checker.check(*a, storage.ctx());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].category == QStringLiteral("incompatibility"));
    CHECK(issues[0].severity == IssueSeverity::Error);
}

TEST_CASE("IncompatibilityIssueChecker reverse-declared incompatibility") {
    auto declarer = make_mod(QStringLiteral("mod.declarer"));
    auto target = make_mod(QStringLiteral("mod.target"));
    declarer->about_rules.incompatible_with.add(QStringLiteral("mod.target"));
    auto storage = make_storage({{declarer->package_id, declarer}, {target->package_id, target}});

    IncompatibilityIssueChecker checker;
    auto issues = checker.check(*target, storage.ctx());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].category == QStringLiteral("incompatibility"));
    CHECK(issues[0].related_package_ids == QList<PackageId>{PackageId(QStringLiteral("mod.declarer"))});
}

TEST_CASE("IncompatibilityIssueChecker reverse-declared deduplicated across origins") {
    auto declarer = make_mod(QStringLiteral("mod.declarer"));
    auto target = make_mod(QStringLiteral("mod.target"));
    const PackageId declarer_pid(QStringLiteral("mod.declarer"));
    const PackageId target_pid(QStringLiteral("mod.target"));
    declarer->about_rules.incompatible_with.add(target_pid.str());

    sort::SortSettings settings;
    settings.use_community_rules = true;
    settings.use_alternative_package_ids = true;
    settings.check_missing_dependencies = true;

    sort::CommunityRule rule;
    rule.package_id = declarer_pid;
    rule.incompatible_with.add(target_pid.str());
    sort::CommunityRules cr{{declarer_pid, rule}};

    auto storage = make_storage({{declarer_pid, declarer}, {target_pid, target}},
                                std::nullopt,
                                &settings,
                                &cr);

    auto incoming_edges = storage.graph.incoming_of_type(target_pid, EdgeType::Incompatibility);
    QSet<EdgeOrigin> origins;
    for (const auto& edge : incoming_edges) {
        origins.insert(edge.origin);
    }
    CHECK(origins == QSet<EdgeOrigin>{EdgeOrigin::AboutXml, EdgeOrigin::CommunityRules});

    auto issues = IncompatibilityIssueChecker().check(*target, storage.ctx());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].related_package_ids == QList<PackageId>{declarer_pid});
}

TEST_CASE("LoadOrderIssueChecker no violations when order satisfies rules") {
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    a->about_rules.load_after.add(QStringLiteral("mod.b"));
    const QList<PackageId> ordered{PackageId(QStringLiteral("mod.b")), PackageId(QStringLiteral("mod.a"))};
    auto storage = make_storage({{a->package_id, a}, {b->package_id, b}}, ordered);

    LoadOrderIssueChecker checker;
    CHECK(checker.check(*a, storage.ctx()).isEmpty());
}

TEST_CASE("LoadOrderIssueChecker load-after violation reported") {
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    a->about_rules.load_after.add(QStringLiteral("mod.b"));
    const QList<PackageId> ordered{PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))};
    auto storage = make_storage({{a->package_id, a}, {b->package_id, b}}, ordered);

    LoadOrderIssueChecker checker;
    auto issues = checker.check(*a, storage.ctx());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].category == QStringLiteral("load_order"));
    CHECK(issues[0].severity == IssueSeverity::Warning);
}

TEST_CASE("LoadOrderIssueChecker load-before violation reported") {
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    a->about_rules.load_before.add(QStringLiteral("mod.b"));
    // A should load before B, but appears after
    const QList<PackageId> ordered{PackageId(QStringLiteral("mod.b")), PackageId(QStringLiteral("mod.a"))};
    auto storage = make_storage({{a->package_id, a}, {b->package_id, b}}, ordered);

    LoadOrderIssueChecker checker;
    auto issues = checker.check(*a, storage.ctx());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].category == QStringLiteral("load_order"));
    CHECK(issues[0].severity == IssueSeverity::Warning);
}

TEST_CASE("CycleIssueChecker no cycles") {
    auto a = make_mod(QStringLiteral("mod.a"));
    auto storage = make_storage({{a->package_id, a}});
    CycleIssueChecker checker;
    CHECK(checker.check(*a, storage.ctx()).isEmpty());
}

TEST_CASE("CycleIssueChecker reports cycle membership") {
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    auto storage = make_storage({{a->package_id, a}, {b->package_id, b}});
    storage.cycles = {{PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))}};

    CycleIssueChecker checker;
    auto issues = checker.check(*a, storage.ctx());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].category == QStringLiteral("cycle"));
    CHECK(issues[0].severity == IssueSeverity::Warning);
}

TEST_CASE("CycleIssueChecker non-member gets no issue") {
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    auto c = make_mod(QStringLiteral("mod.c"));
    auto storage = make_storage({{a->package_id, a}, {b->package_id, b}, {c->package_id, c}});
    storage.cycles = {{PackageId(QStringLiteral("mod.a")), PackageId(QStringLiteral("mod.b"))}};

    CycleIssueChecker checker;
    CHECK(checker.check(*c, storage.ctx()).isEmpty());
}

TEST_CASE("GameVersionIssueChecker no versions declared is not checked") {
    auto a = make_mod(QStringLiteral("mod.a"));
    a->supported_versions.clear();
    auto storage = make_storage({{a->package_id, a}});

    GameVersionIssueChecker checker;
    CHECK(!checker.should_check(*a, storage.ctx()));
    CHECK(checker.check(*a, storage.ctx()).isEmpty());
}

TEST_CASE("GameVersionIssueChecker matching version yields no issues") {
    auto a = make_mod(QStringLiteral("mod.a"));
    a->supported_versions = {QStringLiteral("1.5")};
    auto storage = make_storage({{a->package_id, a}});
    storage.target_version = QStringLiteral("1.5");

    GameVersionIssueChecker checker;
    CHECK(checker.check(*a, storage.ctx()).isEmpty());
}

TEST_CASE("GameVersionIssueChecker mismatch reported as warning") {
    auto a = make_mod(QStringLiteral("mod.a"));
    a->supported_versions = {QStringLiteral("1.4")};
    auto storage = make_storage({{a->package_id, a}});
    storage.target_version = QStringLiteral("1.5");

    GameVersionIssueChecker checker;
    auto issues = checker.check(*a, storage.ctx());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].category == QStringLiteral("version_mismatch"));
    CHECK(issues[0].severity == IssueSeverity::Warning);
}

TEST_CASE("GameVersionIssueChecker suppressed by no_version_warning") {
    auto a = make_mod(QStringLiteral("mod.a"));
    a->supported_versions = {QStringLiteral("1.4")};
    auto storage = make_storage({{a->package_id, a}});
    storage.target_version = QStringLiteral("1.5");
    storage.no_version_warning.insert(PackageId(QStringLiteral("mod.a")));

    GameVersionIssueChecker checker;
    CHECK(checker.check(*a, storage.ctx()).isEmpty());
}

TEST_CASE("ReplacementIssueChecker no replacement without published file id") {
    auto a = make_mod(QStringLiteral("mod.a"));
    auto storage = make_storage({{a->package_id, a}});
    ReplacementIssueChecker checker;
    CHECK(checker.check(*a, storage.ctx()).isEmpty());
}
