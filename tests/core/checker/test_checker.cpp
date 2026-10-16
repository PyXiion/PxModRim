#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <random>
#include <vector>

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/checker/checker.hpp"
#include "core/checker/graph.hpp"
#include "core/checker/issues.hpp"
#include "core/checker/models.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/sort/config.hpp"

using namespace px::modrim;
using namespace px::modrim::checker;

namespace {

std::shared_ptr<AboutXmlMod> make_mod(const QString& package_id, const QString& name = {}) {
    auto m = std::make_shared<AboutXmlMod>();
    m->package_id = CaseInsensitiveStr(package_id);
    m->name = name.isEmpty() ? package_id : name;
    return m;
}

sort::SortSettings settings() {
    sort::SortSettings s;
    s.use_community_rules = false;
    s.use_alternative_package_ids = true;
    s.check_missing_dependencies = true;
    return s;
}

QHash<QString, ListedModPtr> as_listed(const std::vector<std::shared_ptr<AboutXmlMod>>& mods) {
    QHash<QString, ListedModPtr> result;
    for (std::size_t i = 0; i < mods.size(); ++i) {
        result.insert(QString::number(i), mods[i]);
    }
    return result;
}

QString uuid_for(const QHash<QString, ListedModPtr>& mods, const QString& name) {
    for (auto it = mods.cbegin(); it != mods.cend(); ++it) {
        if (it.value()->name == name) {
            return it.key();
        }
    }
    return {};
}

std::vector<std::unique_ptr<ModIssueChecker>> default_checkers() {
    std::vector<std::unique_ptr<ModIssueChecker>> checkers;
    checkers.push_back(std::make_unique<DependencyIssueChecker>());
    checkers.push_back(std::make_unique<IncompatibilityIssueChecker>());
    checkers.push_back(std::make_unique<LoadOrderIssueChecker>());
    checkers.push_back(std::make_unique<CycleIssueChecker>());
    return checkers;
}

}  // namespace

TEST_CASE("ModChecker basic rebuild") {
    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    DependencyMod dep;
    dep.package_id = CaseInsensitiveStr(QStringLiteral("mod.c"));
    a->about_rules.dependencies.append(dep);

    const auto mods = as_listed({a, b});
    const QStringList active_uuids{
        uuid_for(mods, QStringLiteral("mod.a")),
        uuid_for(mods, QStringLiteral("mod.b")),
    };

    DiagnosticsByUuid collected;
    auto on_change = [&](const DiagnosticsByUuid& diags) {
        for (auto it = diags.cbegin(); it != diags.cend(); ++it) {
            collected.insert(it.key(), it.value());
        }
    };

    ModChecker checker(default_checkers(), settings(), QStringLiteral("1.5"), on_change);
    checker.rebuild(mods, active_uuids);

    const QString a_uuid = uuid_for(mods, QStringLiteral("mod.a"));
    const auto* diag_a = checker.diagnostics_for(a_uuid);
    REQUIRE(diag_a != nullptr);
    CHECK(diag_a->has_errors());
    REQUIRE(diag_a->errors.size() == 1);
    CHECK(diag_a->errors[0].category == QStringLiteral("missing_dependency"));

    const QString b_uuid = uuid_for(mods, QStringLiteral("mod.b"));
    const auto* diag_b = checker.diagnostics_for(b_uuid);
    REQUIRE(diag_b != nullptr);
    CHECK(!diag_b->has_errors());
    CHECK(!diag_b->has_warnings());
}

TEST_CASE("ModChecker toggle mod rebuilds diagnostics and constraints") {
    std::vector<std::unique_ptr<ModIssueChecker>> checkers;
    checkers.push_back(std::make_unique<DependencyIssueChecker>());
    checkers.push_back(std::make_unique<IncompatibilityIssueChecker>());

    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    DependencyMod dep;
    dep.package_id = CaseInsensitiveStr(QStringLiteral("mod.b"));
    a->about_rules.dependencies.append(dep);
    b->about_rules.load_after.add(QStringLiteral("mod.a"));

    const auto mods = as_listed({a, b});
    const QString a_uuid = uuid_for(mods, QStringLiteral("mod.a"));
    const QString b_uuid = uuid_for(mods, QStringLiteral("mod.b"));

    ModChecker checker(std::move(checkers), settings());
    checker.rebuild(mods, {a_uuid, b_uuid});

    checker.toggle_mod(*mods[b_uuid], {a_uuid});
    const auto* diag_disabled = checker.diagnostics_for(a_uuid);
    REQUIRE(diag_disabled != nullptr);
    CHECK(diag_disabled->has_errors());
    CHECK(diag_disabled->errors[0].category == QStringLiteral("missing_dependency"));

    checker.toggle_mod(*mods[b_uuid], {a_uuid, b_uuid});
    const auto* diag_enabled = checker.diagnostics_for(a_uuid);
    REQUIRE(diag_enabled != nullptr);
    CHECK(!diag_enabled->has_errors());

    QSet<PackageId> dependency_sources;
    for (const auto& edge :
         checker.graph().incoming_of_type(PackageId(QStringLiteral("mod.b")), EdgeType::Dependency)) {
        dependency_sources.insert(edge.source);
    }
    CHECK(dependency_sources == QSet<PackageId>{PackageId(QStringLiteral("mod.a"))});

    QSet<PackageId> load_after_targets;
    for (const auto& edge :
         checker.graph().edges_of_type(PackageId(QStringLiteral("mod.b")), EdgeType::LoadAfter)) {
        load_after_targets.insert(edge.target);
    }
    CHECK(load_after_targets == QSet<PackageId>{PackageId(QStringLiteral("mod.a"))});
}

TEST_CASE("ModChecker move mod updates load order") {
    std::vector<std::unique_ptr<ModIssueChecker>> checkers;
    checkers.push_back(std::make_unique<LoadOrderIssueChecker>());

    auto a = make_mod(QStringLiteral("mod.a"));
    auto b = make_mod(QStringLiteral("mod.b"));
    a->about_rules.load_after.add(QStringLiteral("mod.b"));

    // b first, then a -> satisfies load_after
    const auto mods = as_listed({b, a});
    const QString a_uuid = uuid_for(mods, QStringLiteral("mod.a"));
    const QString b_uuid = uuid_for(mods, QStringLiteral("mod.b"));

    ModChecker checker(std::move(checkers), settings());
    checker.rebuild(mods, {b_uuid, a_uuid});

    const auto* initial = checker.diagnostics_for(a_uuid);
    CHECK((initial == nullptr || !initial->has_warnings()));

    // Move a before b (old_index 1 -> new_index 0)
    checker.move_mod(a_uuid, 1, 0);

    const auto* after_move = checker.diagnostics_for(a_uuid);
    REQUIRE(after_move != nullptr);
    CHECK(after_move->has_warnings());
    CHECK(after_move->warnings[0].category == QStringLiteral("load_order"));
}

TEST_CASE("ModChecker reorder matches full rebuild") {
    std::mt19937 rng(0);
    constexpr int kModCount = 12;
    std::vector<std::shared_ptr<AboutXmlMod>> mods_list;
    mods_list.reserve(kModCount);
    for (int i = 0; i < kModCount; ++i) {
        mods_list.push_back(make_mod(QStringLiteral("mod.%1").arg(i)));
    }

    for (int i = 0; i < kModCount; ++i) {
        std::vector<int> others;
        for (int j = 0; j < kModCount; ++j) {
            if (j != i) {
                others.push_back(j);
            }
        }
        std::ranges::shuffle(others, rng);
        mods_list[i]->about_rules.load_after.add(QStringLiteral("mod.%1").arg(others[0]));
        mods_list[i]->about_rules.load_after.add(QStringLiteral("mod.%1").arg(others[1]));
        mods_list[i]->about_rules.load_before.add(QStringLiteral("mod.%1").arg(others[2]));
    }

    const auto mods = as_listed(mods_list);
    QStringList order;
    for (int i = 0; i < kModCount; ++i) {
        order.append(QString::number(i));
    }

    ModChecker incremental(default_checkers(), settings());
    incremental.rebuild(mods, order);

    for (int step = 0; step < 30; ++step) {
        if (std::uniform_real_distribution<double>(0.0, 1.0)(rng) < 0.5) {
            std::uniform_int_distribution<qsizetype> pick(0, order.size() - 1);
            const qsizetype i = pick(rng);
            const qsizetype j = pick(rng);
            order.swapItemsAt(i, j);
        } else {
            std::uniform_int_distribution<qsizetype> pick(0, order.size() - 1);
            const qsizetype from = pick(rng);
            const qsizetype to = pick(rng);
            order.insert(to, order.takeAt(from));
        }
        incremental.reorder(order);

        ModChecker full(default_checkers(), settings());
        full.rebuild(mods, order);

        for (const QString& uuid : order) {
            const auto* got = incremental.diagnostics_for(uuid);
            const auto* want = full.diagnostics_for(uuid);
            REQUIRE(got != nullptr);
            REQUIRE(want != nullptr);

            CHECK(got->errors == want->errors);
            CHECK(got->warnings == want->warnings);
        }
    }
}
