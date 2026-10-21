#include <QAbstractItemModel>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "ui/panels/mod_info_data.hpp"
#include "ui/plugins/workshop/models.hpp"

using namespace px::modrim::workshop;

namespace {
CatalogMod mod(std::string id = "1") {
  CatalogMod value;
  value.id = std::move(id);
  value.title = "Mod " + value.id;
  value.author = Author{"1", "Author", std::nullopt};
  value.description = "[b]Description[/b]";
  value.workshop_url =
      "https://steamcommunity.com/sharedfiles/filedetails/?id=" + value.id;
  value.tags = {"Utility"};
  value.supported_versions = {"1.5"};
  value.updated_at = 100;
  value.file_size = "1048576";
  return value;
}

InstallState missing(const CatalogMod&) { return InstallState::kMissing; }
InstallState installed(const CatalogMod&) { return InstallState::kInstalled; }
InstallState outdated(const CatalogMod&) { return InstallState::kOutdated; }

CardContext context(std::function<InstallState(const CatalogMod&)> state,
                    QString game = QStringLiteral("1.5")) {
  CardContext value;
  value.gameVersion = std::move(game);
  value.installState = std::move(state);
  return value;
}

int role(const CatalogListModel& model, const QByteArray& name) {
  const auto names = model.roleNames();
  for (auto it = names.cbegin(); it != names.cend(); ++it)
    if (it.value() == name) return it.key();
  FAIL("Missing role " << name.constData());
  return -1;
}

// Reads a one-row model through the QML roles, so assertions cover what the
// delegates see rather than the card struct.
class CardView {
 public:
  CardView(const CatalogEntry& entry, const CardContext& snapshot)
      : m_entry(entry), m_card(makeCard(entry, snapshot)), m_model(&m_owner) {
    m_model.setPage({m_card}, 1, std::nullopt);
  }

  QVariant operator[](const char* name) const {
    return m_model.data(m_model.index(0), role(m_model, name));
  }
  QVariantMap detail() const { return cardDetail(m_card, m_entry); }

 private:
  QObject m_owner;
  CatalogEntry m_entry;
  CatalogCard m_card;
  CatalogListModel m_model;
};
}  // namespace

TEST_CASE("workshop model rejects unsafe URLs and preserves web URLs") {
  for (const auto* url :
       {"javascript:alert(1)", "file:///tmp/test", "data:text/plain,hi",
        "//example.com", "https:///path", "https://example.com/\nsecret",
        "https://[bad", "https://example.com/a b"}) {
    CAPTURE(url);
    REQUIRE(safeUrl(QString::fromUtf8(url)).isEmpty());
  }
  for (const auto* url : {"https://example.com/path", "http://example.com",
                          "HTTPS://example.com"}) {
    REQUIRE(safeUrl(QString::fromUtf8(url)) == QString::fromUtf8(url));
  }
}

TEST_CASE("workshop catalog roles paging append deduplication and reset") {
  QObject owner;
  CatalogListModel model(&owner);
  const auto ctx = context(outdated, "1.6");
  const QList<CatalogCard> cards{makeCard(CatalogEntry{mod("1")}, ctx),
                                 makeCard(CatalogEntry{mod("2")}, ctx),
                                 makeCard(CatalogEntry{mod("3")}, ctx)};
  int notifications = 0;
  QObject::connect(&model, &CatalogListModel::changed,
                   [&] { ++notifications; });
  model.setPage({cards[0], cards[1]}, 10, QStringLiteral("page2"));
  REQUIRE(model.hasMore());
  REQUIRE(model.cursor() == std::optional<QString>(QStringLiteral("page2")));
  REQUIRE(model.count() == 2);
  REQUIRE(model.total() == 10);
  QSet<QString> names;
  for (const auto& name : model.roleNames())
    names.insert(QString::fromUtf8(name));
  const QSet<QString> expected{
      "itemId",      "kind",         "title",       "author",
      "previewUrl",  "collage",      "sourceLabel", "versions",
      "tags",        "state",        "active",      "stateLabel",
      "actionLabel", "incompatible", "memberCount", "description",
      "workshopUrl", "fileSize",     "votes",       "votesUp",
      "votesDown",   "queued",       "favourite",   "summary",
      "compatLabel"};
  REQUIRE(names == expected);
  REQUIRE(role(model, "itemId") == Qt::UserRole + 1);
  REQUIRE(role(model, "compatLabel") == Qt::UserRole + 25);
  const int idRole = role(model, "itemId");
  REQUIRE(model.data(model.index(0), idRole).toString() == "1");
  model.setPage({cards[1], cards[2], cards[2]}, 10, std::nullopt, true);
  REQUIRE(model.count() == 3);
  REQUIRE_FALSE(model.hasMore());
  REQUIRE(model.data(model.index(2), idRole).toString() == "3");
  REQUIRE_FALSE(model.data({}, idRole).isValid());
  REQUIRE_FALSE(model.data(model.index(0), Qt::DisplayRole).isValid());
  REQUIRE_FALSE(model.data(model.index(0), Qt::UserRole + 26).isValid());
  REQUIRE(model.rowCount(model.index(0)) == 0);
  REQUIRE(model.rowCount() == 3);
  model.setPage({}, 0, std::nullopt);
  REQUIRE(model.count() == 0);
  REQUIRE(model.total() == 0);
  REQUIRE(notifications == 3);
}

TEST_CASE(
    "workshop refreshing a page changes only changed rows without resetting") {
  CatalogListModel model;
  auto first = mod("1"), second = mod("2"), third = mod("3");
  const auto ctx = context(missing, "1.5");
  auto cards = [&] {
    return QList<CatalogCard>{makeCard(CatalogEntry{first}, ctx),
                              makeCard(CatalogEntry{second}, ctx),
                              makeCard(CatalogEntry{third}, ctx)};
  };
  model.setPage(cards(), 3, std::nullopt);
  int resets = 0;
  QList<int> changed;
  QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
  QObject::connect(&model, &QAbstractItemModel::dataChanged,
                   [&](const QModelIndex& firstIndex, const QModelIndex&) {
                     changed.append(firstIndex.row());
                   });
  first.subscriptions = 500;
  second.title = "Renamed";
  model.setPage(cards(), 3, std::nullopt);
  REQUIRE(resets == 0);
  REQUIRE(changed == QList<int>{1});
  REQUIRE(model.data(model.index(1), role(model, "title")).toString() ==
          "Renamed");
  auto reversed = cards();
  std::reverse(reversed.begin(), reversed.end());
  model.setPage(reversed, 3, std::nullopt);
  REQUIRE(resets == 1);
  model.refresh([](const CatalogCard& card) {
    auto copy = card;
    copy.queued = true;
    return copy;
  });
  REQUIRE(model.data(model.index(0), role(model, "queued")).toBool());
  REQUIRE(changed.back() == 0);
  model.setPage({}, 0, std::nullopt);
  changed.clear();
  model.refresh([](const CatalogCard& card) { return card; });
  REQUIRE(changed.isEmpty());
}

TEST_CASE("workshop paging append preserves existing rows and selection") {
  QObject owner;
  CatalogListModel model(&owner);
  const auto ctx = context(missing, "1.5");
  model.setPage({makeCard(CatalogEntry{mod("1")}, ctx)}, 3,
                QStringLiteral("page2"));
  const QPersistentModelIndex selected(model.index(0));
  int resets = 0, insertions = 0;
  QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
  QObject::connect(&model, &QAbstractItemModel::rowsInserted,
                   [&] { ++insertions; });
  model.setPage({makeCard(CatalogEntry{mod("2")}, ctx),
                 makeCard(CatalogEntry{mod("3")}, ctx)},
                3, std::nullopt, true);
  REQUIRE(resets == 0);
  REQUIRE(insertions == 1);
  REQUIRE(selected.isValid());
  REQUIRE(selected.row() == 0);
  REQUIRE(model.data(selected, role(model, "itemId")).toString() == "1");
  REQUIRE(model.count() == 3);
  REQUIRE_FALSE(model.hasMore());
}

TEST_CASE(
    "workshop card state precedence resolves preparing then downloading then "
    "queued") {
  const auto value = mod();
  auto ctx = context(missing, "1.5");
  ctx.preparingIds = {"1"};
  ctx.downloadingIds = {"1"};
  ctx.queuedIds = {"1"};
  CardView preparing(value, ctx);
  REQUIRE(preparing["queued"].toBool());
  REQUIRE(preparing["actionLabel"].toString() == "Preparing…");
  ctx.preparingIds.clear();
  CardView downloading(value, ctx);
  REQUIRE(downloading["queued"].toBool());
  REQUIRE(downloading["actionLabel"].toString() == "Downloading");
  ctx.downloadingIds.clear();
  CardView queued(value, ctx);
  REQUIRE(queued["queued"].toBool());
  REQUIRE(queued["actionLabel"].toString() == "Queued");
}

TEST_CASE("workshop installed mods ignore queue state but collections do not") {
  const auto value = mod();
  auto ctx = context(installed, "1.5");
  ctx.downloadingIds = {"1"};
  CardView modCard(value, ctx);
  REQUIRE_FALSE(modCard["queued"].toBool());
  REQUIRE(modCard["actionLabel"].toString() == "Installed");
  REQUIRE(modCard["state"].toString() == "installed");

  CatalogCollection collection;
  collection.id = "c";
  collection.member_ids = {"1", "2"};
  auto memberCtx = context(missing, "1.5");
  memberCtx.installedIds = {"1"};
  CardView partial(collection, memberCtx);
  REQUIRE(partial["state"].toString() == "missing");
  REQUIRE(partial["actionLabel"].toString() == "Complete download");
  memberCtx.installedIds = {"1", "2"};
  CardView complete(collection, memberCtx);
  REQUIRE(complete["state"].toString() == "installed");
  REQUIRE(complete["actionLabel"].toString() == "Installed");
  memberCtx.downloadingIds = {"2"};
  CardView downloading(collection, memberCtx);
  REQUIRE(downloading["state"].toString() == "installed");
  REQUIRE(downloading["queued"].toBool());
  REQUIRE(downloading["actionLabel"].toString() == "Downloading");
  memberCtx.downloadingIds.clear();
  memberCtx.favouriteIds = {"c"};
  memberCtx.activeIds = {"1"};
  CardView favourite(collection, memberCtx);
  REQUIRE(favourite["favourite"].toBool());
  REQUIRE_FALSE(favourite["active"].toBool());
}

TEST_CASE(
    "workshop mod rows preserve state sizes versions descriptions and "
    "compatibility") {
  auto value = mod();
  CardView view(value, context(outdated, "1.6"));
  REQUIRE(view["actionLabel"].toString() == "Update");
  REQUIRE(view["stateLabel"].toString() == "Update available");
  REQUIRE(view["incompatible"].toBool());
  REQUIRE(view["fileSize"].toString() == "1.0 MB");
  REQUIRE(view["description"].toString().isEmpty());
  REQUIRE(view.detail().value("description").toString() ==
          px::modrim::descriptionToHtml(
              QString::fromStdString(value.description), "bbcode"));
  value.supported_versions = {"1.5", "1.10", "1.2", "1.5", "1.2.1"};
  value.tags = {"Mod", "1.5", "Utility", "1.10", "1.2", "1.2.1"};
  CardView sorted(value, context(missing, "1.5"));
  REQUIRE(sorted["versions"].toString() == "1.2, 1.2.1, 1.5, 1.10");
  REQUIRE(sorted["tags"].toString() == QStringLiteral("Mod · Utility"));
  struct Compatibility {
    std::vector<std::string> versions;
    QString game;
    bool incompatible;
    QString label;
  };
  const std::vector<Compatibility> compatibilityCases{
      {{"1.5"}, "1.5", false, "Works with 1.5"},
      {{"1.4"}, "1.5", true, "Not for 1.5"},
      {{}, "1.5", false, "Version not stated"},
      {{"1.5"}, "", false, ""}};
  for (const auto& scenario : compatibilityCases) {
    value.supported_versions = scenario.versions;
    value.incompatible = scenario.incompatible;
    CardView compatible(value, context(missing, scenario.game));
    REQUIRE(compatible["compatLabel"].toString() == scenario.label);
  }
  value.author = {};
  value.file_size.reset();
  CardView unknown(value, context(missing, ""));
  REQUIRE(unknown["author"].toString() == "Unknown author");
  REQUIRE(unknown["fileSize"].toString() == "Unknown");
}

TEST_CASE("workshop vote approval uses counts and formats thousands") {
  struct Case {
    std::optional<Votes> votes;
    QString approval, up, down;
  };
  const std::vector<Case> cases{
      {std::nullopt, "Unrated", "0", "0"},
      {Votes{0, 0, 100}, "Unrated", "0", "0"},
      {Votes{0, 1, {}}, "0%", "0", "1"},
      {Votes{1, 0, {}}, "100%", "1", "0"},
      {Votes{1, 2, {}}, "33%", "1", "2"},
      {Votes{2, 1, {}}, "67%", "2", "1"},
      {Votes{1000, 3000, {}}, "25%", "1,000", "3,000"}};
  for (const auto& scenario : cases) {
    auto value = mod();
    value.votes = scenario.votes;
    CardView view(value, context(installed, "1.5"));
    REQUIRE(view["votes"].toString() == scenario.approval);
    REQUIRE(view["votesUp"].toString() == scenario.up);
    REQUIRE(view["votesDown"].toString() == scenario.down);
    REQUIRE(view["actionLabel"].toString() == "Installed");
  }
}

TEST_CASE(
    "workshop summaries strip bbcode media and truncate at a word boundary") {
  REQUIRE(summarize("[h1]Title[/h1] [img]https://x.test/a.png[/img] Adds "
                    "[url=https://x.test]a thing[/url]") ==
          "Title Adds a thing");
  REQUIRE(summarize("[b]x[/b] <b>kept</b>", "text") == "[b]x[/b] <b>kept</b>");
  const QString summary = summarize(QStringLiteral("word ").repeated(100));
  REQUIRE(summary.endsWith(QStringLiteral("word…")));
  REQUIRE(summary.size() <= 141);
}

TEST_CASE(
    "workshop collection rows select previews filter collage and display "
    "server size") {
  CatalogCollection value;
  value.id = "picked:x";
  value.source = CollectionSource::kPicked;
  value.title = "C";
  value.author.name = "Curator";
  value.description = "[b]Description[/b]";
  value.previews = {{PreviewType::kVideo, "https://youtube.test/v"},
                    {PreviewType::kImage, "https://images.test/own.png"}};
  value.member_ids = {"10"};
  value.member_count = 1;
  value.member_previews = {"https://images.test/m.png", "javascript:alert(1)"};
  const auto neverInstall = [](const CatalogMod&) {
    FAIL("Collections must not request install state");
    return InstallState::kMissing;
  };
  CardView view(value, context(neverInstall, "1.6"));
  REQUIRE(view["previewUrl"].toString() == "https://images.test/own.png");
  REQUIRE(view["collage"].toStringList() ==
          QStringList{"https://images.test/m.png"});
  REQUIRE(view["votes"].toString() == "Unrated");
  REQUIRE(view["votesUp"].toString() == "0");
  REQUIRE(view["votesDown"].toString() == "0");
  REQUIRE_FALSE(view["active"].toBool());
  REQUIRE(view["fileSize"].toString() == "Unknown");
  REQUIRE(view["sourceLabel"].toString() == "PxModRim pick");
  REQUIRE(view["actionLabel"].toString() == "Download all");
  REQUIRE(view["memberCount"].toInt() == 1);
  REQUIRE(view.detail().value("description").toString() ==
          px::modrim::descriptionToHtml("[b]Description[/b]", "text"));
  value.total_size = "3145728";
  value.preview_url = "https://images.test/explicit.png";
  value.source = CollectionSource::kSteam;
  CardView sized(value, context(neverInstall, "1.6"));
  REQUIRE(sized["fileSize"].toString() == "3.0 MB");
  REQUIRE(sized["previewUrl"].toString() == "https://images.test/explicit.png");
  REQUIRE(sized["sourceLabel"].toString() == "Steam collection");
}

TEST_CASE(
    "workshop collection version declarations override conflicting member "
    "summary") {
  CatalogCollection value;
  value.no_common_version = true;
  CardView noCommon(value, context(missing, "1.6"));
  REQUIRE(noCommon["compatLabel"].toString() == "No common version");
  REQUIRE(noCommon["incompatible"].toBool());
  value.no_common_version = false;
  CardView common(value, context(missing, "1.6"));
  REQUIRE(common["compatLabel"].toString() == "Version not stated");
  REQUIRE_FALSE(common["incompatible"].toBool());
  value.no_common_version = true;
  const std::vector<
      std::pair<std::vector<std::string>, std::vector<std::string>>>
      cases{
          {{}, {"Mod", "1.6"}}, {{"1.5"}, {"Mod", "1.6"}}, {{"1.6"}, {"Mod"}}};
  for (const auto& [versions, tags] : cases) {
    value.supported_versions = versions;
    value.tags = tags;
    CardView view(value, context(missing, "1.6"));
    REQUIRE_FALSE(view["incompatible"].toBool());
    REQUIRE(view["compatLabel"].toString() == "Works with 1.6");
    REQUIRE(view["versions"].toString().contains("1.6"));
    REQUIRE(view["tags"].toString() == "Mod");
  }
}
