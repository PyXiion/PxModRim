#include <catch2/catch_test_macros.hpp>

#include <utility>

#include <QStringList>

#include "ui/plugins/workshop/installed.hpp"

using px::modrim::workshop::CatalogMod;
using px::modrim::workshop::InstalledList;
using px::modrim::workshop::kPage;

namespace {
CatalogMod installedMod(std::string id, std::string title, std::string author = "Someone",
                         std::vector<std::string> tags = {}) {
    CatalogMod mod;
    mod.id = std::move(id);
    mod.title = std::move(title);
    mod.author.name = std::move(author);
    mod.tags = std::move(tags);
    return mod;
}

QStringList ids(const std::vector<CatalogMod>& mods) {
    QStringList result;
    for (const auto& mod : mods) result.append(QString::fromStdString(mod.id));
    return result;
}
}  // namespace

TEST_CASE("workshop installed search matches title author id and tags case insensitively") {
    InstalledList installed;
    REQUIRE_FALSE(installed.loaded());
    REQUIRE(installed.total() == 0);
    REQUIRE(installed.visible().empty());
    REQUIRE(installed.nextPage().empty());
    installed.search("");
    installed.replace({installedMod("11", "Harmony", "Brrainz"), installedMod("12", "HugsLib", "Someone", {"Library"}),
                       installedMod("999", "Other")});
    REQUIRE(installed.loaded());
    REQUIRE(installed.mods().size() == 3);
    const std::vector<std::pair<QString, QStringList>> cases{{"harm", {"11"}}, {"BRRAINZ", {"11"}},
        {"library", {"12"}}, {"999", {"999"}}, {"", {"11", "12", "999"}}, {"nothing", {}}};
    for (const auto& [query, expected] : cases) {
        CAPTURE(query.toStdString());
        installed.search(query);
        REQUIRE(ids(installed.visible()) == expected);
        REQUIRE(installed.total() == expected.size());
    }
}

TEST_CASE("workshop installed paging reveals one page and search restarts it") {
    InstalledList installed;
    std::vector<CatalogMod> mods;
    for (int i = 0; i < kPage * 2 + 5; ++i) mods.push_back(installedMod(std::to_string(i), "Mod"));
    installed.replace(mods);
    REQUIRE(installed.visible().size() == kPage);
    REQUIRE(installed.hasMore());
    const auto second = installed.nextPage();
    REQUIRE(second.size() == kPage);
    REQUIRE(second.front().id == std::to_string(kPage));
    REQUIRE(installed.nextPage().size() == 5);
    REQUIRE_FALSE(installed.hasMore());
    REQUIRE(installed.nextPage().empty());
    installed.search("");
    REQUIRE(installed.visible().size() == kPage);
    REQUIRE(installed.total() == kPage * 2 + 5);
    installed.nextPage();
    installed.resetPaging();
    REQUIRE(installed.visible().size() == kPage);
    REQUIRE(installed.hasMore());
}

TEST_CASE("workshop replacing installed mods preserves query and current paging") {
    InstalledList installed;
    installed.search("mod");
    std::vector<CatalogMod> mods;
    for (int i = 0; i < kPage * 3; ++i) mods.push_back(installedMod(std::to_string(i), "Mod"));
    installed.replace(mods);
    installed.nextPage();
    installed.replace(mods);
    REQUIRE(installed.visible().size() == kPage * 2);
    installed.replace({installedMod("match", "Mod"), installedMod("other", "Other")});
    REQUIRE(ids(installed.visible()) == QStringList{"match"});
    REQUIRE(installed.total() == 1);
    REQUIRE_FALSE(installed.hasMore());
    REQUIRE(installed.nextPage().empty());
    installed.replace({});
    REQUIRE_FALSE(installed.loaded());
    REQUIRE(installed.visible().empty());
}
