#include <catch2/catch_test_macros.hpp>

#include <memory>

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include "core/services/sort_service.hpp"

using px::modrim::AboutXmlMod;
using px::modrim::CaseInsensitiveStr;
using px::modrim::DependencyMod;
using px::modrim::ListedMod;
using px::modrim::ListedModPtr;
using px::modrim::SortService;
using px::modrim::sort::PackageId;

namespace {

std::shared_ptr<AboutXmlMod> about(const QString& name, const QString& package_id) {
    auto mod = std::make_shared<AboutXmlMod>();
    mod->name = name;
    mod->package_id = CaseInsensitiveStr(package_id);
    mod->provider_id = QStringLiteral("stub");
    mod->valid = true;
    return mod;
}

// `deps` maps a dependency package id to its alternative package ids.
std::shared_ptr<AboutXmlMod> about_with_deps(const QString& name, const QString& package_id,
                                             const QHash<QString, QStringList>& deps) {
    auto mod = about(name, package_id);
    for (auto it = deps.cbegin(); it != deps.cend(); ++it) {
        DependencyMod dep;
        dep.name = QStringLiteral("Dep of %1").arg(name);
        dep.package_id = CaseInsensitiveStr(it.key());
        for (const auto& alt : it.value()) {
            dep.alternative_package_ids.add(alt);
        }
        mod->about_rules.dependencies.append(std::move(dep));
    }
    return mod;
}

ListedModPtr listed(const QString& name) {
    auto mod = std::make_shared<ListedMod>();
    mod->name = name;
    mod->provider_id = QStringLiteral("stub");
    return mod;
}

QHash<QString, ListedModPtr> mods(std::initializer_list<std::pair<QString, ListedModPtr>> entries) {
    QHash<QString, ListedModPtr> result;
    for (const auto& [uuid, mod] : entries) {
        result.insert(uuid, mod);
    }
    return result;
}

}  // namespace

TEST_CASE("duplicate inactive copy does not replace the active uuid") {
    const auto all = mods({
        {QStringLiteral("uuid-active"), about("Active Copy", "shared.package")},
        {QStringLiteral("uuid-inactive"), about("Inactive Copy", "shared.package")},
    });

    const auto ordered = SortService::order_uuids({PackageId(QStringLiteral("shared.package"))}, all,
                                                  {QStringLiteral("uuid-active")});

    CHECK(ordered == QStringList{QStringLiteral("uuid-active")});
}

TEST_CASE("order_uuids follows the sorted package ids and drops unknown ones") {
    const auto all = mods({
        {QStringLiteral("uuid-a"), about("Mod A", "mod.a")},
        {QStringLiteral("uuid-b"), about("Mod B", "mod.b")},
    });

    const auto ordered = SortService::order_uuids(
        {PackageId(QStringLiteral("mod.b")), PackageId(QStringLiteral("ghost")),
         PackageId(QStringLiteral("mod.a"))},
        all, {QStringLiteral("uuid-a"), QStringLiteral("uuid-b")});

    CHECK(ordered == QStringList{QStringLiteral("uuid-b"), QStringLiteral("uuid-a")});
}

TEST_CASE("order_uuids skips active mods that are not about.xml mods") {
    const auto all = mods({
        {QStringLiteral("uuid-folder"), listed("Folder Mod")},
        {QStringLiteral("uuid-a"), about("Mod A", "mod.a")},
    });

    const auto ordered = SortService::order_uuids(
        {PackageId(QStringLiteral("mod.a"))}, all,
        {QStringLiteral("uuid-folder"), QStringLiteral("uuid-a")});

    CHECK(ordered == QStringList{QStringLiteral("uuid-a")});
}

TEST_CASE("missing dependencies: no dependencies yields nothing") {
    const auto all = mods({
        {QStringLiteral("uuid-a"), about("Mod A", "mod.a")},
        {QStringLiteral("uuid-b"), about("Mod B", "mod.b")},
    });

    CHECK(SortService::missing_dependency_uuids(
              all, {QStringLiteral("uuid-a"), QStringLiteral("uuid-b")})
              .isEmpty());
}

TEST_CASE("missing dependency that exists as an inactive mod is enabled") {
    const auto all = mods({
        {QStringLiteral("uuid-a"), about_with_deps("Mod A", "mod.a", {{"mod.b", {}}})},
        {QStringLiteral("uuid-b"), about("Mod B", "mod.b")},
    });

    CHECK(SortService::missing_dependency_uuids(all, {QStringLiteral("uuid-a")}) ==
          QStringList{QStringLiteral("uuid-b")});
}

TEST_CASE("missing dependency that is not among the known mods is ignored") {
    const auto all = mods({
        {QStringLiteral("uuid-a"), about_with_deps("Mod A", "mod.a", {{"mod.b", {}}})},
    });

    CHECK(SortService::missing_dependency_uuids(all, {QStringLiteral("uuid-a")}).isEmpty());
}

TEST_CASE("missing dependency that is already active is not enabled again") {
    const auto all = mods({
        {QStringLiteral("uuid-a"), about_with_deps("Mod A", "mod.a", {{"mod.b", {}}})},
        {QStringLiteral("uuid-b"), about("Mod B", "mod.b")},
    });

    CHECK(SortService::missing_dependency_uuids(
              all, {QStringLiteral("uuid-a"), QStringLiteral("uuid-b")})
              .isEmpty());
}

TEST_CASE("dependency satisfied by an active alternative package id is not missing") {
    const auto all = mods({
        {QStringLiteral("uuid-a"),
         about_with_deps("Mod A", "mod.a", {{"mod.b", {QStringLiteral("mod.c")}}})},
        {QStringLiteral("uuid-c"), about("Mod C", "mod.c")},
    });

    CHECK(SortService::missing_dependency_uuids(
              all, {QStringLiteral("uuid-a"), QStringLiteral("uuid-c")})
              .isEmpty());
}

TEST_CASE("non about.xml mods in the active set contribute no dependencies") {
    const auto all = mods({
        {QStringLiteral("uuid-folder"), listed("Folder Mod")},
        {QStringLiteral("uuid-a"), about("Mod A", "mod.a")},
    });

    CHECK(SortService::missing_dependency_uuids(
              all, {QStringLiteral("uuid-folder"), QStringLiteral("uuid-a")})
              .isEmpty());
}

TEST_CASE("a dependency shared by two active mods is listed once") {
    const auto all = mods({
        {QStringLiteral("uuid-a"), about_with_deps("Mod A", "mod.a", {{"mod.c", {}}})},
        {QStringLiteral("uuid-b"), about_with_deps("Mod B", "mod.b", {{"mod.c", {}}})},
        {QStringLiteral("uuid-c"), about("Mod C", "mod.c")},
    });

    CHECK(SortService::missing_dependency_uuids(
              all, {QStringLiteral("uuid-a"), QStringLiteral("uuid-b")}) ==
          QStringList{QStringLiteral("uuid-c")});
}

TEST_CASE("an empty active set needs nothing") {
    const auto all = mods({
        {QStringLiteral("uuid-a"), about_with_deps("Mod A", "mod.a", {{"mod.b", {}}})},
        {QStringLiteral("uuid-b"), about("Mod B", "mod.b")},
    });

    CHECK(SortService::missing_dependency_uuids(all, {}).isEmpty());
}

TEST_CASE("a mod without dependencies adds nothing to the result") {
    const auto all = mods({
        {QStringLiteral("uuid-a"), about("Mod A", "mod.a")},
        {QStringLiteral("uuid-b"), about_with_deps("Mod B", "mod.b", {{"mod.a", {}}})},
    });

    CHECK(SortService::missing_dependency_uuids(
              all, {QStringLiteral("uuid-a"), QStringLiteral("uuid-b")})
              .isEmpty());
}
