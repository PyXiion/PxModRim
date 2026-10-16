#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>

#include <QSet>

#include "ui/panels/mod_info_data.hpp"
#include "ui/plugins/workshop/models.hpp"

using namespace px::modrim::workshop;
using namespace px::modrim::workshop;

namespace {
CatalogMod mod(std::string id = "1") {
    CatalogMod value;
    value.id = std::move(id);
    value.title = "Mod " + value.id;
    value.author = Author{"1", "Author", std::nullopt};
    value.description = "[b]Description[/b]";
    value.workshop_url = "https://steamcommunity.com/sharedfiles/filedetails/?id=" + value.id;
    value.tags = {"Utility"};
    value.supported_versions = {"1.5"};
    value.updated_at = 100;
    value.file_size = "1048576";
    return value;
}

InstallState missing(const CatalogMod&) { return InstallState::missing; }
InstallState outdated(const CatalogMod&) { return InstallState::outdated; }

int role(const CatalogListModel& model, const QByteArray& name) {
    const auto names = model.roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) if (it.value() == name) return it.key();
    FAIL("Missing role " << name.constData());
    return -1;
}
}  // namespace

TEST_CASE("workshop model rejects unsafe URLs and preserves web URLs") {
    for (const auto* url : {"javascript:alert(1)", "file:///tmp/test", "data:text/plain,hi", "//example.com",
                            "https:///path", "https://example.com/\nsecret", "https://[bad", "https://example.com/a b"}) {
        CAPTURE(url);
        REQUIRE(safe_url(QString::fromUtf8(url)).isEmpty());
    }
    for (const auto* url : {"https://example.com/path", "http://example.com", "HTTPS://example.com"}) {
        REQUIRE(safe_url(QString::fromUtf8(url)) == QString::fromUtf8(url));
    }
}

TEST_CASE("workshop catalog roles paging append deduplication and reset") {
    QObject owner;
    CatalogListModel model(&owner);
    const QList<QVariantMap> rows{item_row(mod("1"), outdated, "1.6"), item_row(mod("2"), outdated, "1.6"),
                                  item_row(mod("3"), outdated, "1.6")};
    int notifications = 0;
    QObject::connect(&model, &CatalogListModel::changed, [&] { ++notifications; });
    model.set_page({rows[0], rows[1]}, 10, QStringLiteral("page2"));
    REQUIRE(model.hasMore());
    REQUIRE(model.cursor() == std::optional<QString>(QStringLiteral("page2")));
    REQUIRE(model.count() == 2);
    REQUIRE(model.total() == 10);
    QSet<QString> names;
    for (const auto& name : model.roleNames()) names.insert(QString::fromUtf8(name));
    REQUIRE(names == QSet<QString>(rows[0].keyBegin(), rows[0].keyEnd()));
    REQUIRE(role(model, "itemId") == Qt::UserRole + 1);
    REQUIRE(role(model, "compatLabel") == Qt::UserRole + 25);
    const int id_role = role(model, "itemId");
    REQUIRE(model.data(model.index(0), id_role).toString() == "1");
    model.set_page({rows[1], rows[2], rows[2]}, 10, std::nullopt, true);
    REQUIRE(model.count() == 3);
    REQUIRE_FALSE(model.hasMore());
    REQUIRE(model.data(model.index(2), id_role).toString() == "3");
    REQUIRE_FALSE(model.data({}, id_role).isValid());
    REQUIRE_FALSE(model.data(model.index(0), Qt::DisplayRole).isValid());
    REQUIRE_FALSE(model.data(model.index(0), Qt::UserRole + 26).isValid());
    REQUIRE(model.rowCount(model.index(0)) == 0);
    REQUIRE(model.rowCount() == 3);
    model.set_page({}, 0, std::nullopt);
    REQUIRE(model.count() == 0);
    REQUIRE(model.total() == 0);
    REQUIRE(notifications == 3);
}

TEST_CASE("workshop refreshing a page changes only changed rows without resetting") {
    CatalogListModel model;
    auto first = mod("1"), second = mod("2"), third = mod("3");
    auto show = [&] { model.set_page({item_row(first, missing, "1.5"), item_row(second, missing, "1.5"),
                                     item_row(third, missing, "1.5")}, 3, std::nullopt); };
    show();
    int resets = 0;
    QList<int> changed;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
    QObject::connect(&model, &QAbstractItemModel::dataChanged,
                     [&](const QModelIndex& first_index, const QModelIndex&) { changed.append(first_index.row()); });
    first.subscriptions = 500;
    second.title = "Renamed";
    show();
    REQUIRE(resets == 0);
    REQUIRE(changed == QList<int>{1});
    REQUIRE(model.data(model.index(1), role(model, "title")).toString() == "Renamed");
    auto reversed = model.rows();
    std::reverse(reversed.begin(), reversed.end());
    model.set_page(reversed, 3, std::nullopt);
    REQUIRE(resets == 1);
    model.refresh_rows([](const QVariantMap& row) { auto copy = row; copy["queued"] = true; return copy; });
    REQUIRE(model.rows().front().value("queued").toBool());
    REQUIRE(changed.back() == 0);
    model.set_page({}, 0, std::nullopt);
    changed.clear();
    model.refresh_rows([](const QVariantMap& row) { return row; });
    REQUIRE(changed.isEmpty());
}

TEST_CASE("workshop mod rows preserve state sizes versions descriptions and compatibility") {
    auto value = mod();
    auto row = item_row(value, outdated, "1.6");
    REQUIRE(row.value("actionLabel").toString() == "Update");
    REQUIRE(row.value("stateLabel").toString() == "Update available");
    REQUIRE(row.value("incompatible").toBool());
    REQUIRE(row.value("fileSize").toString() == "1.0 MB");
    REQUIRE(row.value("description").toString().isEmpty());
    REQUIRE(item_row(value, missing, "1.5", true).value("description").toString()
            == px::modrim::description_to_html(QString::fromStdString(value.description), "bbcode"));
    value.supported_versions = {"1.5", "1.10", "1.2", "1.5", "1.2.1"};
    value.tags = {"Mod", "1.5", "Utility", "1.10", "1.2", "1.2.1"};
    row = item_row(value, missing, "1.5");
    REQUIRE(row.value("versions").toString() == "1.2, 1.2.1, 1.5, 1.10");
    REQUIRE(row.value("tags").toString() == QStringLiteral("Mod · Utility"));
    struct Compatibility { std::vector<std::string> versions; QString game; bool incompatible; QString label; };
    const std::vector<Compatibility> compatibility_cases{
        {{"1.5"}, "1.5", false, "Works with 1.5"},
        {{"1.4"}, "1.5", true, "Not for 1.5"},
        {{}, "1.5", false, "Version not stated"}, {{"1.5"}, "", false, ""}};
    for (const auto& scenario : compatibility_cases) {
        value.supported_versions = scenario.versions;
        value.incompatible = scenario.incompatible;
        REQUIRE(item_row(value, missing, scenario.game).value("compatLabel").toString() == scenario.label);
    }
    value.author = {};
    value.file_size.reset();
    row = item_row(value, missing, "");
    REQUIRE(row.value("author").toString() == "Unknown author");
    REQUIRE(row.value("fileSize").toString() == "Unknown");
}

TEST_CASE("workshop vote approval uses counts and formats thousands") {
    struct Case { std::optional<Votes> votes; QString approval, up, down; };
    const std::vector<Case> cases{{std::nullopt, "Unrated", "0", "0"}, {Votes{0, 0, 100}, "Unrated", "0", "0"},
        {Votes{0, 1, {}}, "0%", "0", "1"}, {Votes{1, 0, {}}, "100%", "1", "0"},
        {Votes{1, 2, {}}, "33%", "1", "2"}, {Votes{2, 1, {}}, "67%", "2", "1"},
        {Votes{1000, 3000, {}}, "25%", "1,000", "3,000"}};
    for (const auto& scenario : cases) {
        auto value = mod();
        value.votes = scenario.votes;
        const auto row = item_row(value, [](const CatalogMod&) { return InstallState::installed; }, "1.5");
        REQUIRE(row.value("votes").toString() == scenario.approval);
        REQUIRE(row.value("votesUp").toString() == scenario.up);
        REQUIRE(row.value("votesDown").toString() == scenario.down);
        REQUIRE(row.value("actionLabel").toString() == "Installed");
    }
}

TEST_CASE("workshop summaries strip bbcode media and truncate at a word boundary") {
    REQUIRE(summarize("[h1]Title[/h1] [img]https://x.test/a.png[/img] Adds [url=https://x.test]a thing[/url]")
            == "Title Adds a thing");
    REQUIRE(summarize("[b]x[/b] <b>kept</b>", "text") == "[b]x[/b] <b>kept</b>");
    const QString summary = summarize(QStringLiteral("word ").repeated(100));
    REQUIRE(summary.endsWith(QStringLiteral("word…")));
    REQUIRE(summary.size() <= 141);
}

TEST_CASE("workshop collection rows select previews filter collage and display server size") {
    CatalogCollection value;
    value.id = "picked:x";
    value.source = CollectionSource::picked;
    value.title = "C";
    value.author.name = "Curator";
    value.description = "[b]Description[/b]";
    value.previews = {{PreviewType::video, "https://youtube.test/v"}, {PreviewType::image, "https://images.test/own.png"}};
    value.member_ids = {"10"};
    value.member_count = 1;
    value.member_previews = {"https://images.test/m.png", "javascript:alert(1)"};
    const auto never_install = [](const CatalogMod&) { FAIL("Collections must not request install state"); return InstallState::missing; };
    auto row = item_row(value, never_install, "1.6", true);
    REQUIRE(row.value("previewUrl").toString() == "https://images.test/own.png");
    REQUIRE(row.value("collage").toStringList() == QStringList{"https://images.test/m.png"});
    REQUIRE(row.value("votes").toString() == "Unrated");
    REQUIRE(row.value("votesUp").toString() == "0");
    REQUIRE(row.value("votesDown").toString() == "0");
    REQUIRE_FALSE(row.value("active").toBool());
    REQUIRE(row.value("fileSize").toString() == "Unknown");
    REQUIRE(row.value("sourceLabel").toString() == "PxModRim pick");
    REQUIRE(row.value("actionLabel").toString() == "Download all");
    REQUIRE(row.value("memberCount").toInt() == 1);
    REQUIRE(row.value("description").toString() == px::modrim::description_to_html("[b]Description[/b]", "text"));
    value.total_size = "3145728";
    value.preview_url = "https://images.test/explicit.png";
    value.source = CollectionSource::steam;
    row = item_row(value, never_install, "1.6");
    REQUIRE(row.value("fileSize").toString() == "3.0 MB");
    REQUIRE(row.value("previewUrl").toString() == "https://images.test/explicit.png");
    REQUIRE(row.value("sourceLabel").toString() == "Steam collection");
}

TEST_CASE("workshop collection version declarations override conflicting member summary") {
    CatalogCollection value;
    value.no_common_version = true;
    auto row = item_row(value, missing, "1.6");
    REQUIRE(row.value("compatLabel").toString() == "No common version");
    REQUIRE(row.value("incompatible").toBool());
    value.no_common_version = false;
    row = item_row(value, missing, "1.6");
    REQUIRE(row.value("compatLabel").toString() == "Version not stated");
    REQUIRE_FALSE(row.value("incompatible").toBool());
    value.no_common_version = true;
    const std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> cases{
        {{}, {"Mod", "1.6"}}, {{"1.5"}, {"Mod", "1.6"}}, {{"1.6"}, {"Mod"}}};
    for (const auto& [versions, tags] : cases) {
        value.supported_versions = versions;
        value.tags = tags;
        row = item_row(value, missing, "1.6");
        REQUIRE_FALSE(row.value("incompatible").toBool());
        REQUIRE(row.value("compatLabel").toString() == "Works with 1.6");
        REQUIRE(row.value("versions").toString().contains("1.6"));
        REQUIRE(row.value("tags").toString() == "Mod");
    }
}
