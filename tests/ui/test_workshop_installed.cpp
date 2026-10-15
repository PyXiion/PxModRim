#include <catch2/catch_test_macros.hpp>

#include <utility>

#include <QStringList>

#include "ui/plugins/workshop/installed.h"

using px::core::workshop::CatalogMod;
using px::ui::workshop::InstalledList;
using px::ui::workshop::PAGE;

namespace {
CatalogMod installed_mod(std::string id, std::string title, std::string author = "Someone",
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
    REQUIRE(installed.next_page().empty());
    installed.search("");
    installed.replace({installed_mod("11", "Harmony", "Brrainz"), installed_mod("12", "HugsLib", "Someone", {"Library"}),
                       installed_mod("999", "Other")});
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
    for (int i = 0; i < PAGE * 2 + 5; ++i) mods.push_back(installed_mod(std::to_string(i), "Mod"));
    installed.replace(mods);
    REQUIRE(installed.visible().size() == PAGE);
    REQUIRE(installed.has_more());
    const auto second = installed.next_page();
    REQUIRE(second.size() == PAGE);
    REQUIRE(second.front().id == std::to_string(PAGE));
    REQUIRE(installed.next_page().size() == 5);
    REQUIRE_FALSE(installed.has_more());
    REQUIRE(installed.next_page().empty());
    installed.search("");
    REQUIRE(installed.visible().size() == PAGE);
    REQUIRE(installed.total() == PAGE * 2 + 5);
    installed.next_page();
    installed.reset_paging();
    REQUIRE(installed.visible().size() == PAGE);
    REQUIRE(installed.has_more());
}

TEST_CASE("workshop replacing installed mods preserves query and current paging") {
    InstalledList installed;
    installed.search("mod");
    std::vector<CatalogMod> mods;
    for (int i = 0; i < PAGE * 3; ++i) mods.push_back(installed_mod(std::to_string(i), "Mod"));
    installed.replace(mods);
    installed.next_page();
    installed.replace(mods);
    REQUIRE(installed.visible().size() == PAGE * 2);
    installed.replace({installed_mod("match", "Mod"), installed_mod("other", "Other")});
    REQUIRE(ids(installed.visible()) == QStringList{"match"});
    REQUIRE(installed.total() == 1);
    REQUIRE_FALSE(installed.has_more());
    REQUIRE(installed.next_page().empty());
    installed.replace({});
    REQUIRE_FALSE(installed.loaded());
    REQUIRE(installed.visible().empty());
}
