#include <QApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QTemporaryDir>
#include <algorithm>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "../core/workshop/support.hpp"
#include "core/context.hpp"
#include "core/downloads/downloader.hpp"
#include "core/mod_service.hpp"
#include "test_support.hpp"
#include "ui/components/svg_provider.hpp"
#include "ui/context.hpp"
#include "ui/navigation.hpp"
#include "ui/plugins/workshop/plugin.hpp"
#include "ui/plugins/workshop/view.hpp"
#include "ui/theme/qml_theme.hpp"

namespace workshop_view_test {
using namespace px::modrim;
using namespace px::modrim::workshop;
using workshop_test::HttpRequest;
using workshop_test::HttpResponse;
using workshop_test::run;
using workshop_test::wire;

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on the wire
struct Batch {
  std::vector<CatalogMod> items;
  std::vector<std::string> unavailable_ids;
};
// NOLINTEND(readability-identifier-naming)

QVariant card(const CatalogListModel& model, const char* name, int row = 0) {
  const auto names = model.roleNames();
  for (auto it = names.cbegin(); it != names.cend(); ++it)
    if (it.value() == name) return model.data(model.index(row), it.key());
  FAIL("Missing role " << name);
  return {};
}

class DownloaderFixture : public Downloader {
 public:
  explicit DownloaderFixture(DownloadManager& manager)
      : Downloader(QStringLiteral("test_steam"), QStringLiteral("Steam"),
                   manager) {}
  bool isDownloading() const override { return busy; }
  QSet<QString> activeIds() const override { return active; }
  bool accepts(const QString&) const override { return true; }
  std::optional<QString> updatableId(const ListedMod& mod) const override {
    return mod.publishedFileId();
  }
  std::optional<double> lastSynced(const ListedMod&) const override {
    return 50.;
  }
  QCoro::Task<Result<DownloadResult>> downloadMods(QStringList ids) override {
    calls.append(ids);
    co_return DownloadResult{.succeeded = ids, .failed = {}, .changed = {}};
  }
  void cancel() override {}
  bool busy = false;
  QSet<QString> active;
  QList<QStringList> calls;
};

struct PanelFixture {
  QWidget owner;
  QTemporaryDir dir;
  std::unique_ptr<CoreContext> core = px::ui_test::makeCore(AppConfig{});
  AppContext app{*core};
  DownloaderFixture downloader{core->downloads()};
  ConfigService config{dir.path()};
  WorkshopCatalog catalog{config, core->downloads(),
                          [this] { return core->allMods().values(); }};
  CatalogMod mod;
  CatalogMod dependency;
  CatalogCollection collection;
  bool fail = false;
  bool complete = false;
  std::function<void(const HttpRequest&)> onRequest;
  workshop_test::FakeCatalog server{
      [this](const HttpRequest& request) { return answer(request); }};
  QQmlEngine* engine = nullptr;
  std::unique_ptr<WorkshopViewPanel> view;

  PanelFixture() {
    REQUIRE(QApplication::instance());
    REQUIRE(dir.isValid());
    core->downloads().registerDownloader(downloader);
    mod.id = "1";
    mod.title = "<b>Untrusted title</b>";
    mod.author.name = "Author";
    mod.description = "[b]Safe description[/b]";
    mod.tags = {"Utility"};
    mod.supported_versions = {"1.5"};
    mod.updated_at = 100;
    mod.file_size = "1024";
    mod.dependencies = {"2"};
    mod.workshop_url =
        "https://steamcommunity.com/sharedfiles/filedetails/?id=1";
    dependency = mod;
    dependency.id = "2";
    dependency.title = "Dependency";
    dependency.dependencies.clear();
    collection.id = "picked:test";
    collection.source = CollectionSource::kPicked;
    collection.title = "Test collection";
    collection.author.name = "Curator";
    collection.description = "<b>Plain text</b>";
    collection.supported_versions = {"1.6"};
    collection.member_ids = {"1", "missing"};
    collection.member_count = 2;
    catalog.setBaseUrl(server.url());
    engine = createQmlEngine(&owner);
    engine->rootContext()->setContextProperty(QStringLiteral("Theme"),
                                              new px::modrim::Theme(engine));
    view = std::make_unique<WorkshopViewPanel>(*core, engine, &owner, app,
                                               catalog);
    view->resize(1100, 800);
    run(view->load());
  }
  ~PanelFixture() {
    view.reset();
    run(catalog.shutdown());
  }
  HttpResponse answer(const HttpRequest& request) {
    if (onRequest) onRequest(request);
    if (fail) return {503, R"({"error":{"message":"Catalog unavailable"}})"};
    const auto path = request.path();
    if (path == QStringLiteral("/catalog/discover"))
      return {200, wire(Discover{{{mod}, 1, {}}, {{collection}, 1, {}}})};
    if (path == QStringLiteral("/catalog/mods")) {
      const bool second =
          request.query().hasQueryItem(QStringLiteral("cursor"));
      return {200, wire(CatalogPage<CatalogMod>{
                       {second ? dependency : mod},
                       2,
                       second ? std::nullopt
                              : std::optional<std::string>("second")})};
    }
    if (path == QStringLiteral("/catalog/collections"))
      return {200, wire(CatalogPage<CatalogCollection>{{collection}, 1, {}})};
    if (path == QStringLiteral("/catalog/mods/1")) return {200, wire(mod)};
    if (path == QStringLiteral("/catalog/mods/2"))
      return {200, wire(dependency)};
    if (path == QStringLiteral("/catalog/collections/picked/test")) {
      auto item = collection;
      if (complete) {
        item.member_ids = {"1"};
        item.member_count = 1;
      }
      return {200, wire(CollectionDetail{
                       item,
                       {mod},
                       complete ? std::vector<std::string>{}
                                : std::vector<std::string>{"missing"},
                       complete})};
    }
    if (path == QStringLiteral("/catalog/mods/batch"))
      return {200, wire(Batch{{mod}, {}})};
    if (path == QStringLiteral("/catalog/resolve")) {
      const bool collectionRequest =
          request.body.find("picked:test") != std::string::npos;
      const bool incomplete = collectionRequest && !complete;
      return {200, wire(ResolvedDownload{
                       {"1"},
                       {{"1", mod}, {"2", dependency}},
                       {"2", "1"},
                       incomplete ? std::vector<std::string>{"missing"}
                                  : std::vector<std::string>{},
                       incomplete ? std::vector<std::string>{"picked:test"}
                                  : std::vector<std::string>{},
                       !incomplete})};
    }
    return {404, R"({"error":{"message":"Not found"}})"};
  }
  QString install(const QString& id,
                  const QString& provider = QStringLiteral("steam")) {
    auto installed = std::make_shared<ListedMod>();
    REQUIRE(
        installed->setModPath(dir.filePath(provider + QLatin1Char('/') + id))
            .has_value());
    installed->providerId = provider;
    installed->name = id;
    auto mods = core->allMods();
    const auto uuid = installed->uuid();
    mods.insert(uuid, installed);
    core->load(std::move(mods), core->activeUuids());
    catalog.invalidateInstalled();
    return uuid;
  }
  qsizetype requests(const QString& path) const {
    return std::ranges::count_if(server.requests, [&path](const auto& request) {
      return request.path() == path;
    });
  }
};
}  // namespace workshop_view_test

using namespace workshop_view_test;

TEST_CASE(
    "workshop view loads discovery and paginates filtered catalog results") {
  PanelFixture f;
  CHECK(f.view->findChild<QQuickWidget*>()->status() == QQuickWidget::Ready);
  CHECK(f.view->modsModel->count() == 1);
  CHECK(f.view->collectionsModel->count() == 1);
  run(f.view->selectTabAsync(QStringLiteral("Mods")));
  run(f.view->filterItems({}, QStringLiteral("1.6"), QStringLiteral("all"),
                          QStringLiteral("popular"), {}));
  run(f.view->load(true));
  CHECK(f.view->modsModel->count() == 2);
  CHECK_FALSE(f.view->modsModel->hasMore());
  CHECK(f.server.requests.back().query().queryItemValue(
            QStringLiteral("cursor")) == QStringLiteral("second"));
  CHECK(f.server.requests.back().query().queryItemValue(
            QStringLiteral("version")) == QStringLiteral("1.6"));
  f.fail = true;
  run(f.view->filterItems(QStringLiteral("uncached"), QStringLiteral("1.6"),
                          QStringLiteral("all"), QStringLiteral("popular"),
                          {}));
  CHECK(f.view->error() == QStringLiteral("Catalog unavailable"));
  f.fail = false;
  run(f.view->retryLoad());
  CHECK(f.view->error().isEmpty());
  CHECK(f.view->modsModel->count() == 1);
}

TEST_CASE("workshop detail escapes descriptions and lists dependencies") {
  PanelFixture f;
  run(f.view->openItemAsync(QStringLiteral("1"), QStringLiteral("mod")));
  CHECK(f.view->hasDetail());
  CHECK(f.view->membersModel->count() == 1);
  CHECK(f.view->detail()
            .value(QStringLiteral("description"))
            .toString()
            .contains(QStringLiteral("<b>Safe description</b>")));
  run(f.view->openItemAsync(QStringLiteral("picked:test"),
                            QStringLiteral("collection")));
  CHECK(f.view->detail()
            .value(QStringLiteral("description"))
            .toString()
            .contains(QStringLiteral("&lt;b&gt;Plain text&lt;/b&gt;")));
  CHECK(f.view->detail()
            .value(QStringLiteral("warning"))
            .toString()
            .contains(QStringLiteral("missing")));
}

TEST_CASE(
    "incomplete workshop downloads require explicit partial approval and never "
    "activate") {
  PanelFixture f;
  const auto uuid = f.install(QStringLiteral("1"));
  run(f.view->downloadItemAsync(QStringLiteral("picked:test"),
                                QStringLiteral("collection")));
  CHECK(f.view->pendingPlan());
  CHECK(f.view->canDownloadAvailable());
  CHECK(f.downloader.calls.isEmpty());
  f.view->downloadAvailable();
  CHECK_FALSE(f.view->pendingPlan());
  REQUIRE(f.downloader.calls.size() == 1);
  CHECK(f.downloader.calls.front() ==
        QStringList{QStringLiteral("2"), QStringLiteral("1")});
  CHECK_FALSE(f.core->activeUuids().contains(uuid));
  CHECK(f.view->notice().contains(QStringLiteral("Mods are not activated")));
}

TEST_CASE("workshop partial plan cancellation clears notice without queuing") {
  PanelFixture f;
  run(f.view->downloadItemAsync(QStringLiteral("picked:test"),
                                QStringLiteral("collection")));
  f.view->dismissPlan();
  CHECK_FALSE(f.view->pendingPlan());
  CHECK(f.view->notice().isEmpty());
  CHECK(f.downloader.calls.isEmpty());
}

TEST_CASE("workshop collection detail falls back to member preview collage") {
  PanelFixture f;
  f.mod.preview_url = "https://example.com/preview.png";
  run(f.view->openItemAsync(QStringLiteral("picked:test"),
                            QStringLiteral("collection")));
  CHECK(f.view->detail().value(QStringLiteral("collage")).toStringList() ==
        QStringList{QStringLiteral("https://example.com/preview.png")});
}

TEST_CASE(
    "workshop collection listing detects partial and fully installed members") {
  PanelFixture f;
  f.collection.member_ids = {"1", "2"};
  f.collection.member_count = 2;
  f.install(QStringLiteral("1"));
  run(f.view->selectTabAsync(QStringLiteral("Collections")));
  const auto& model = *f.view->collectionsModel;
  CHECK(card(model, "actionLabel").toString() ==
        QStringLiteral("Complete download"));
  CHECK(card(model, "state").toString() == QStringLiteral("missing"));
  f.install(QStringLiteral("2"));
  CHECK(card(model, "actionLabel").toString() == QStringLiteral("Installed"));
  CHECK(card(model, "state").toString() == QStringLiteral("installed"));
  f.downloader.active = {QStringLiteral("1")};
  f.core->downloads().busyChanged.notify(true);
  CHECK(card(model, "queued").toBool());
  CHECK(card(model, "actionLabel").toString() == QStringLiteral("Downloading"));
}

TEST_CASE("disabled workshop catalog does not issue requests") {
  PanelFixture f;
  f.catalog.setBaseUrl({});
  const auto before = f.server.requests.size();
  run(f.view->load());
  CHECK_FALSE(f.view->configured());
  CHECK_FALSE(f.view->busy());
  CHECK(f.server.requests.size() == before);
}

TEST_CASE("workshop settings apply catalog URL only on save") {
  PanelFixture f;
  CatalogSettingsSection section(f.catalog, &f.owner);
  REQUIRE(section.set(QStringLiteral("url"),
                      QStringLiteral("  https://example.com/catalog///  ")));
  CHECK(f.catalog.settings().url == f.server.url().toStdString());
  f.view->teardown();
  REQUIRE(section.apply().has_value());
  CHECK(f.catalog.settings().url == "https://example.com/catalog");
}

TEST_CASE("installed workshop search is local and cached across revisits") {
  PanelFixture f;
  f.install(QStringLiteral("1"));
  run(f.view->selectTabAsync(QStringLiteral("Installed")));
  REQUIRE(f.view->modsModel->count() == 1);
  const auto fetched = f.requests(QStringLiteral("/catalog/mods/batch"));
  run(f.view->filterItems(QStringLiteral("absent"), {}, QStringLiteral("all"),
                          QStringLiteral("popular"), {}));
  CHECK(f.view->modsModel->count() == 0);
  run(f.view->filterItems(QStringLiteral("Author"), {}, QStringLiteral("all"),
                          QStringLiteral("popular"), {}));
  CHECK(f.view->modsModel->count() == 1);
  run(f.view->selectTabAsync(QStringLiteral("Mods")));
  run(f.view->selectTabAsync(QStringLiteral("Installed")));
  CHECK(f.requests(QStringLiteral("/catalog/mods/batch")) == fetched);
}

TEST_CASE(
    "workshop activation toggles one copy and removes every active duplicate") {
  PanelFixture f;
  const auto first = f.install(QStringLiteral("1"));
  const auto second =
      f.install(QStringLiteral("1"), QStringLiteral("steamcmd"));
  run(f.view->toggleActivationAsync(QStringLiteral("1")));
  REQUIRE(f.core->activeUuids().size() == 1);
  CHECK((f.core->activeUuids().contains(first) ||
         f.core->activeUuids().contains(second)));
  f.core->setActive({first, second});
  run(f.view->toggleActivationAsync(QStringLiteral("1")));
  CHECK(f.core->activeUuids().isEmpty());
  run(f.view->toggleActivationAsync(QStringLiteral("absent")));
  CHECK(f.core->activeUuids().isEmpty());
}

TEST_CASE(
    "external workshop activation refreshes listing detail and dependencies") {
  PanelFixture f;
  const auto first = f.install(QStringLiteral("1"));
  const auto second = f.install(QStringLiteral("2"));
  run(f.view->openItemAsync(QStringLiteral("1"), QStringLiteral("mod")));
  f.core->setActive({first, second});
  CHECK(f.view->detail().value(QStringLiteral("active")).toBool());
  CHECK(card(*f.view->modsModel, "active").toBool());
  CHECK(card(*f.view->membersModel, "active").toBool());
}

TEST_CASE("workshop tags accumulate excluding supported version tags") {
  PanelFixture f;
  f.mod.tags = {"1.5", "Utility", "Animals"};
  run(f.view->selectTabAsync(QStringLiteral("Mods")));
  f.dependency.tags = {"Library"};
  run(f.view->openItemAsync(QStringLiteral("1"), QStringLiteral("mod")));
  CHECK(f.view->tagOptions() ==
        QStringList{QStringLiteral("All tags"), QStringLiteral("Animals"),
                    QStringLiteral("Library"), QStringLiteral("Utility")});
}

TEST_CASE(
    "workshop detail back walks history before restoring listing filters") {
  PanelFixture f;
  run(f.view->selectTabAsync(QStringLiteral("Mods")));
  run(f.view->filterItems(QStringLiteral("query"), QStringLiteral("1.6"),
                          QStringLiteral("all"), QStringLiteral("popular"),
                          {}));
  run(f.view->openItemAsync(QStringLiteral("picked:test"),
                            QStringLiteral("collection")));
  run(f.view->openItemAsync(QStringLiteral("1"), QStringLiteral("mod")));
  CHECK(f.view->backLabel() == QStringLiteral("Back to Test collection"));
  run(f.view->goBack());
  CHECK(f.view->detail().value(QStringLiteral("itemId")) ==
        QStringLiteral("picked:test"));
  run(f.view->goBack());
  CHECK_FALSE(f.view->hasDetail());
  CHECK(f.view->tab() == QStringLiteral("Mods"));
  CHECK(f.view->searchQuery() == QStringLiteral("query"));
}

TEST_CASE("workshop routes tabs items and settings through app navigator") {
  PanelFixture f;
  QString route;
  f.app.setNavigator([&route](const QString& value) { route = value; });
  f.view->openItem(QStringLiteral("picked:test"), QStringLiteral("collection"));
  REQUIRE(parseRoute(route));
  CHECK(parseRoute(route)->path == QStringList{QStringLiteral("collection"),
                                               QStringLiteral("picked:test")});
  run(f.view->followRoute(parseRoute(route)->path));
  CHECK(f.view->hasDetail());
  run(f.view->followRoute({QStringLiteral("mods")}));
  CHECK(f.view->tab() == QStringLiteral("Mods"));
  CHECK_FALSE(f.view->hasDetail());
  f.view->openSettings();
  REQUIRE(parseRoute(route));
  CHECK(parseRoute(route)->viewId == QStringLiteral("settings"));
}

TEST_CASE("workshop starred collections stay in favourites until unstarred") {
  PanelFixture f;
  f.view->toggleFavourite(QStringLiteral("picked:test"));
  run(f.view->selectTabAsync(QStringLiteral("Favourites")));
  REQUIRE(f.view->collectionsModel->count() == 1);
  CHECK(card(*f.view->collectionsModel, "favourite").toBool());
  run(f.view->filterItems(QStringLiteral("not present"), {},
                          QStringLiteral("all"), QStringLiteral("popular"),
                          {}));
  CHECK(f.view->collectionsModel->count() == 0);
  run(f.view->filterItems({}, {}, QStringLiteral("all"),
                          QStringLiteral("popular"), {}));
  f.view->toggleFavourite(QStringLiteral("picked:test"));
  CHECK(f.view->collectionsModel->count() == 0);
}

TEST_CASE(
    "fully installed workshop pack activates all or only its members and "
    "dependencies") {
  PanelFixture f;
  f.complete = true;
  const auto first = f.install(QStringLiteral("1"));
  const auto duplicate =
      f.install(QStringLiteral("1"), QStringLiteral("steamcmd"));
  const auto dependency = f.install(QStringLiteral("2"));
  const auto other = f.install(QStringLiteral("3"));
  f.core->setActive({duplicate, other});
  run(f.view->openItemAsync(QStringLiteral("picked:test"),
                            QStringLiteral("collection")));
  CHECK(f.view->detail().value(QStringLiteral("state")) ==
        QStringLiteral("installed"));
  run(f.view->toggleCollectionAsync());
  CHECK(f.core->activeUuids().contains(duplicate));
  CHECK(f.core->activeUuids().contains(dependency));
  CHECK(f.core->activeUuids().contains(other));
  CHECK_FALSE(f.core->activeUuids().contains(first));
  run(f.view->toggleCollectionAsync(true));
  CHECK_FALSE(f.core->activeUuids().contains(other));
  f.core->setActive({first, duplicate, dependency});
  run(f.view->toggleCollectionAsync());
  CHECK(f.core->activeUuids().isEmpty());
}

TEST_CASE(
    "partly installed and incomplete workshop collections cannot activate") {
  PanelFixture f;
  f.install(QStringLiteral("1"));
  for (bool complete : {false, true}) {
    f.complete = complete;
    f.catalog.setBaseUrl(f.server.url() +
                         (complete ? QStringLiteral("/") : QString{}));
    run(f.view->openItemAsync(QStringLiteral("picked:test"),
                              QStringLiteral("collection")));
    run(f.view->toggleCollectionAsync(true));
    CHECK(f.core->activeUuids().isEmpty());
  }
}

TEST_CASE(
    "workshop installed events preserve open details until back reloads the "
    "list") {
  PanelFixture f;
  f.install(QStringLiteral("1"));
  run(f.view->selectTabAsync(QStringLiteral("Installed")));
  run(f.view->openItemAsync(QStringLiteral("1"), QStringLiteral("mod")));
  const auto before = f.requests(QStringLiteral("/catalog/mods/batch"));
  f.install(QStringLiteral("2"));
  CHECK(f.view->hasDetail());
  CHECK(f.requests(QStringLiteral("/catalog/mods/batch")) == before);
  run(f.view->goBack());
  CHECK_FALSE(f.view->hasDetail());
  CHECK(f.requests(QStringLiteral("/catalog/mods/batch")) == before + 1);
}

TEST_CASE(
    "workshop teardown disconnects events and blocks late fetch and plan "
    "completion") {
  PanelFixture f;
  const auto uuid = f.install(QStringLiteral("1"));
  f.onRequest = [&f](const HttpRequest& request) {
    if (request.path() == QStringLiteral("/catalog/mods")) f.view->teardown();
  };
  run(f.view->selectTabAsync(QStringLiteral("Mods")));
  f.view->teardown();
  run(f.view->toggleActivationAsync(QStringLiteral("1")));
  CHECK_FALSE(f.core->activeUuids().contains(uuid));
  f.catalog.queueChanged.notify({});
  f.catalog.installedChanged.notify({});
  f.core->downloads().busyChanged.notify(false);
  CHECK(f.view->modsModel->count() == 1);
}

TEST_CASE("workshop discards download plan completing after view destruction") {
  PanelFixture f;
  f.onRequest = [&f](const HttpRequest& request) {
    if (request.path() == QStringLiteral("/catalog/resolve")) f.view.reset();
  };
  run(f.view->downloadItemAsync(QStringLiteral("1"), QStringLiteral("mod")));
  CHECK_FALSE(f.view);
  CHECK(f.downloader.calls.isEmpty());
}

TEST_CASE(
    "workshop detail metadata retains all tags and numerically sorted "
    "versions") {
  PanelFixture f;
  f.mod.tags = {"1.6", "Utility", "Animals"};
  f.mod.supported_versions = {"1.10", "1.6", "1.5"};
  run(f.view->openItemAsync(QStringLiteral("1"), QStringLiteral("mod")));
  CHECK(f.view->detail().value(QStringLiteral("versions")) ==
        QStringLiteral("1.5, 1.6, 1.10"));
  CHECK(f.view->detail().value(QStringLiteral("tags")) ==
        QStringLiteral("Utility · Animals"));
}
