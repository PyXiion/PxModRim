#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <chrono>
#include <coroutine>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <glaze/glaze.hpp>

#include "../test_support.hpp"
#include "core/config.hpp"
#include "core/downloads/downloader.hpp"
#include "core/downloads/manager.hpp"
#include "core/downloads/types.hpp"
#include "core/file_io.hpp"
#include "core/models/metadata/structures.hpp"
#include "core/workshop/catalog.hpp"
#include "core/workshop/client.hpp"
#include "core/workshop/types.hpp"
#include "support.hpp"

using namespace px::modrim;
using namespace px::modrim::workshop;
using test_support::TestApplication;
using test_support::writeBytes;
using workshop_test::decodeMod;
using workshop_test::FakeCatalog;
using workshop_test::HttpRequest;
using workshop_test::HttpResponse;
using workshop_test::run;
using workshop_test::wire;

namespace {
// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on the wire
struct BatchWire {
  std::vector<CatalogMod> items;
  std::vector<std::string> unavailable_ids;
};
// NOLINTEND(readability-identifier-naming)

struct IdsBatchPayload {
  std::vector<std::string> ids;
};

struct FakeDownloader : public Downloader {
  struct DownloadGate {
    std::coroutine_handle<> waiter;

    // NOLINTBEGIN(readability-identifier-naming): coroutine awaiter protocol
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) noexcept {
      waiter = handle;
    }
    void await_resume() const noexcept {}
    // NOLINTEND(readability-identifier-naming)

    void release() { std::exchange(waiter, {}).resume(); }
  };

  FakeDownloader(DownloadManager& manager, std::map<QString, double> synced)
      : Downloader(QStringLiteral("fake_steam"), QStringLiteral("Steam"),
                   manager),
        synced(std::move(synced)) {}

  bool isDownloading() const override { return isBusy; }
  QSet<QString> activeIds() const override { return active; }
  bool accepts(const QString&) const override { return true; }
  std::optional<QString> updatableId(const ListedMod& mod) const override {
    return mod.publishedFileId();
  }
  std::optional<double> lastSynced(const ListedMod& mod) const override {
    const auto id = mod.publishedFileId();
    if (!id) {
      return std::nullopt;
    }
    const auto found = synced.find(*id);
    return found == synced.end() ? std::nullopt
                                 : std::optional<double>(found->second);
  }

  QCoro::Task<Result<DownloadResult>> downloadMods(QStringList ids) override {
    recordedCalls.push_back(ids);
    if (gate) {
      isBusy = true;
      active = QSet<QString>(ids.begin(), ids.end());
      busyChanged.notify(true);
      co_await *gate;
      isBusy = false;
      active.clear();
      busyChanged.notify(false);
    }
    co_return DownloadResult{.succeeded = ids, .failed = {}, .changed = {}};
  }

  void cancel() override { ++cancelCalls; }

  std::map<QString, double> synced;
  bool isBusy = false;
  QSet<QString> active;
  std::vector<QStringList> recordedCalls;
  int cancelCalls = 0;
  std::optional<DownloadGate> gate;
};

ListedModPtr makeMod(const QString& path,
                     const QString& provider = QStringLiteral("local")) {
  auto mod = std::make_shared<ListedMod>();
  REQUIRE(mod->setModPath(path).has_value());
  mod->providerId = provider;
  return mod;
}

CatalogMod modWith(std::string id, std::optional<std::int64_t> updated) {
  CatalogMod mod = decodeMod(workshop_test::kModJson);
  mod.id = std::move(id);
  mod.updated_at = updated;
  return mod;
}

struct ContextFixture {
  QTemporaryDir dir;
  std::unique_ptr<ConfigService> config;
  DownloadManager downloads;
  std::unique_ptr<FakeDownloader> downloader;
  QList<ListedModPtr> mods;

  ContextFixture() {
    REQUIRE(dir.isValid());
    config = std::make_unique<ConfigService>(dir.path());
    downloader = std::make_unique<FakeDownloader>(
        downloads, std::map<QString, double>{
                       {QStringLiteral("1"), 200.0},
                       {QStringLiteral("2"), 50.0},
                   });
    downloads.registerDownloader(*downloader);

    // Mods 1, 2, 4 are local; 3 is workshop; NotWorkshop has no numeric folder
    // name.
    mods.append(makeMod(dir.filePath(QStringLiteral("Mods/1"))));
    mods.append(makeMod(dir.filePath(QStringLiteral("Mods/2"))));
    mods.append(makeMod(dir.filePath(QStringLiteral("Workshop/3")),
                        QStringLiteral("workshop")));
    mods.append(makeMod(dir.filePath(QStringLiteral("Mods/4"))));
    mods.append(makeMod(dir.filePath(QStringLiteral("Mods/NotWorkshop"))));
  }
};

// NOLINTBEGIN(readability-identifier-naming): fields are JSON keys on the wire
struct ResolvePayload {
  std::vector<std::string> ids;
  std::vector<std::string> collection_ids;
};
// NOLINTEND(readability-identifier-naming)

}  // namespace

TEST_CASE("workshop install state uses sync timestamp, not directory mtime") {
  ContextFixture fixture;
  WorkshopCatalog catalog(fixture.config.get(), fixture.downloads,
                          [&] { return fixture.mods; });

  CHECK(catalog.installState(modWith("1", 100)) == InstallState::kInstalled);
  CHECK(catalog.installState(modWith("2", 100)) == InstallState::kOutdated);
  // 3 has no sync record, so it cannot be outdated.
  CHECK(catalog.installState(modWith("3", 100)) == InstallState::kInstalled);
  CHECK(catalog.installState(modWith("4", 100)) == InstallState::kInstalled);
  // 5 is not installed at all.
  CHECK(catalog.installState(modWith("5", 100)) == InstallState::kMissing);
  // When updated_at is null or equal to sync, the mod is installed.
  CHECK(catalog.installState(modWith("2", std::nullopt)) ==
        InstallState::kInstalled);
  CHECK(catalog.installState(modWith("2", 50)) == InstallState::kInstalled);
}

TEST_CASE("workshop plan keeps dependency order and incomplete flags") {
  TestApplication app;
  ContextFixture fixture;

  FakeCatalog server([](const HttpRequest&) -> HttpResponse {
    ResolvedDownload answer;
    answer.roots = {"5", "picked:starter"};
    for (const char* id : {"1", "2", "3", "4", "5"}) {
      CatalogMod m = modWith(id, 100);
      m.title = std::string("Title ") + id;
      answer.items.emplace(id, std::move(m));
    }
    answer.items.emplace("picked:starter", workshop_test::decodeCollection(
                                               workshop_test::kCollectionJson));
    answer.mod_ids = {"1", "2", "3", "4", "5"};
    answer.unavailable_ids = {"99"};
    answer.incomplete_collection_ids = {"picked:starter"};
    answer.is_complete = false;
    return {200, wire(answer)};
  });

  WorkshopCatalog catalog(fixture.config.get(), fixture.downloads,
                          [&] { return fixture.mods; });
  catalog.setBaseUrl(server.url());

  const auto plan = run(
      catalog.plan({QStringLiteral("5")}, {QStringLiteral("picked:starter")}));
  REQUIRE(plan.has_value());
  CHECK(plan->to_download ==
        QStringList{QStringLiteral("2"), QStringLiteral("5")});
  CHECK(plan->already_current == QStringList{QStringLiteral("1"),
                                             QStringLiteral("3"),
                                             QStringLiteral("4")});
  CHECK(plan->unavailable_ids == QStringList{QStringLiteral("99")});
  CHECK(plan->incomplete_collection_ids ==
        QStringList{QStringLiteral("picked:starter")});
  CHECK_FALSE(plan->is_complete);
  CHECK(plan->titles.value(QStringLiteral("2")) == QStringLiteral("Title 2"));

  const auto payload =
      glz::read_json<ResolvePayload>(server.requests.back().body);
  REQUIRE(payload.has_value());
  CHECK(payload->ids == std::vector<std::string>{"5"});
  CHECK(payload->collection_ids == std::vector<std::string>{"picked:starter"});
}

TEST_CASE("workshop installed metadata uses batch") {
  TestApplication app;
  ContextFixture fixture;

  std::vector<std::string> requestedIds;
  FakeCatalog server([&](const HttpRequest& request) -> HttpResponse {
    const auto body = glz::read_json<IdsBatchPayload>(request.body);
    REQUIRE(body.has_value());
    requestedIds = body->ids;
    BatchWire answer{
        .items = {modWith("2", 100)},
        .unavailable_ids = {"1", "3", "4"},
    };
    return {200, wire(answer)};
  });

  WorkshopCatalog catalog(fixture.config.get(), fixture.downloads,
                          [&] { return fixture.mods; });
  catalog.setBaseUrl(server.url());

  const auto installed = run(catalog.installedWithUpdates());
  REQUIRE(installed.has_value());
  CHECK(requestedIds == std::vector<std::string>{"1", "2", "3", "4"});
  REQUIRE(installed->size() == 1);
  CHECK(catalog.installState(installed->front()) == InstallState::kOutdated);
}

TEST_CASE("workshop download delegates without side effects") {
  TestApplication app;
  ContextFixture fixture;
  WorkshopCatalog catalog(fixture.config.get(), fixture.downloads,
                          [&] { return fixture.mods; });

  DownloadPlan plan{
      .to_download = {QStringLiteral("2")},
      .already_current = {QStringLiteral("1")},
      .titles = {{QStringLiteral("2"), QStringLiteral("Mod")}},
      .is_complete = true,
  };
  const auto result = run(catalog.download(plan));
  REQUIRE(result.has_value());
  CHECK(result->succeeded == QStringList{QStringLiteral("2")});
  REQUIRE(fixture.downloader->recordedCalls.size() == 1);
  CHECK(fixture.downloader->recordedCalls[0] ==
        QStringList{QStringLiteral("2")});
}

TEST_CASE("workshop empty URL is disabled without creating a client") {
  TestApplication app;
  ContextFixture fixture;
  WorkshopCatalog catalog(fixture.config.get(), fixture.downloads,
                          [&] { return fixture.mods; });
  catalog.setBaseUrl(QString());
  CHECK_FALSE(catalog.configured());

  const auto discover = run(catalog.discover());
  REQUIRE_FALSE(discover.has_value());
  CHECK(discover.error().message.contains(QStringLiteral("disabled")));

  const auto installed = run(catalog.installedWithUpdates());
  REQUIRE_FALSE(installed.has_value());
  CHECK(installed.error().message.contains(QStringLiteral("disabled")));

  const auto plan = run(catalog.plan({QStringLiteral("1")}, {}));
  REQUIRE_FALSE(plan.has_value());
  CHECK(plan.error().message.contains(QStringLiteral("disabled")));
}

TEST_CASE("workshop configuration and installed events") {
  TestApplication app;
  ContextFixture fixture;
  WorkshopCatalog catalog(fixture.config.get(), fixture.downloads,
                          [&] { return fixture.mods; });

  std::vector<QString> urlChanges;
  int installedInvalidated = 0;
  catalog.catalogUrlChanged.connect(
      [&](const QString& url) { urlChanges.push_back(url); });
  catalog.installedChanged.connect(
      [&](std::monostate) { ++installedInvalidated; });

  catalog.setBaseUrl(QStringLiteral(" https://other.example/ "));
  CHECK(catalog.settings().url == "https://other.example");
  const auto stored = fixture.config->loadExisting<CatalogSettings>(
      QStringLiteral("plugins/workshop_catalog.json"));
  REQUIRE(stored.has_value());
  REQUIRE(stored->has_value());
  CHECK((*stored)->url == "https://other.example");

  catalog.setBaseUrl(QString());
  CHECK_FALSE(catalog.configured());

  // Firing download_finished and manual invalidation both emit
  // installed_changed.
  catalog.invalidateInstalled();
  fixture.downloads.downloadFinished.notify(DownloadResult{});
  CHECK(installedInvalidated == 2);
  CHECK(urlChanges == std::vector<QString>{
                          QStringLiteral("https://other.example"), QString()});
}

TEST_CASE("workshop cached serves remembered value before refreshing") {
  TestApplication app;
  ContextFixture fixture;
  WorkshopCatalog catalog(fixture.config.get(), fixture.downloads,
                          [&] { return fixture.mods; });

  std::vector<std::string> sequence{"first", "second", "second"};
  std::size_t nextIdx = 0;
  int calls = 0;

  auto produce = [&]() -> QCoro::Task<CatalogResult<std::string>> {
    ++calls;
    co_return sequence.at(nextIdx++);
  };

  auto seen = [&](double freshFor) {
    std::vector<std::string> out;
    const auto done = run(catalog.cached<std::string>(
        QStringLiteral("k"), produce,
        [&](const std::string& v) { out.push_back(v); }, freshFor));
    REQUIRE(done.has_value());
    return out;
  };

  CHECK(seen(300.0) == std::vector<std::string>{"first"});
  CHECK(seen(300.0) == std::vector<std::string>{"first"});
  CHECK(calls == 1);
  CHECK(seen(0.0) == std::vector<std::string>{"first", "second"});
  CHECK(calls == 2);
  CHECK(seen(0.0) == std::vector<std::string>{"second"});
  CHECK(calls == 3);
}

TEST_CASE("workshop enqueue runs batches in order after the running download") {
  TestApplication app;
  ContextFixture fixture;
  WorkshopCatalog catalog(fixture.config.get(), fixture.downloads,
                          [&] { return fixture.mods; });

  fixture.downloader->isBusy = true;
  DownloadPlan first{.to_download = {QStringLiteral("5"), QStringLiteral("6")}};
  DownloadPlan second{
      .to_download = {QStringLiteral("6"), QStringLiteral("7")}};
  catalog.enqueue(first);
  catalog.enqueue(second);

  CHECK(catalog.queuedIds() == QSet<QString>{QStringLiteral("5"),
                                             QStringLiteral("6"),
                                             QStringLiteral("7")});
  CHECK(fixture.downloader->recordedCalls.empty());

  // Once the downloads report idle, the queue drains both batches in order.
  fixture.downloader->isBusy = false;
  fixture.downloads.busyChanged.notify(false);

  CHECK(fixture.downloader->recordedCalls.size() == 2);
  CHECK(fixture.downloader->recordedCalls[0] ==
        QStringList{QStringLiteral("5"), QStringLiteral("6")});
  CHECK(fixture.downloader->recordedCalls[1] ==
        QStringList{QStringLiteral("7")});
  CHECK(catalog.queuedIds().isEmpty());
}

TEST_CASE("workshop shutdown waits for its cancelled download to finish") {
  TestApplication app;
  ContextFixture fixture;
  fixture.downloader->gate.emplace();
  auto catalog = std::make_unique<WorkshopCatalog>(
      fixture.config.get(), fixture.downloads, [&] { return fixture.mods; });
  catalog->enqueue(DownloadPlan{.to_download = {QStringLiteral("5")}});
  catalog->enqueue(DownloadPlan{.to_download = {QStringLiteral("6")}});
  REQUIRE(fixture.downloader->isBusy);
  REQUIRE(fixture.downloader->gate->waiter);

  bool finished = false;
  auto finishShutdown = [&]() -> QCoro::Task<void> {
    co_await catalog->shutdown();
    finished = true;
  };
  auto shutdown = finishShutdown();
  CHECK_FALSE(finished);
  CHECK(fixture.downloader->cancelCalls == 1);
  CHECK(catalog->queuedIds().isEmpty());

  SECTION("the catalog survives until shutdown completes") {}
  SECTION("the catalog is destroyed before the download finishes") {
    catalog.reset();
  }

  fixture.downloader->gate->release();
  run(std::move(shutdown));
  CHECK(finished);
  CHECK_FALSE(fixture.downloader->isBusy);
  CHECK(fixture.downloader->recordedCalls ==
        std::vector<QStringList>{{QStringLiteral("5")}});

  QEventLoop loop;
  QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
  loop.exec();
}

TEST_CASE("workshop destruction leaves a suspended download safe to finish") {
  TestApplication app;
  ContextFixture fixture;
  fixture.downloader->gate.emplace();
  auto catalog = std::make_unique<WorkshopCatalog>(
      fixture.config.get(), fixture.downloads, [&] { return fixture.mods; });
  int queueChanges = 0;
  catalog->queueChanged.connect([&](std::monostate) { ++queueChanges; });
  catalog->enqueue(DownloadPlan{.to_download = {QStringLiteral("5")}});
  REQUIRE(fixture.downloader->gate->waiter);
  const int changesBeforeDestruction = queueChanges;

  catalog.reset();
  fixture.downloader->gate->release();

  QEventLoop loop;
  QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
  loop.exec();
  CHECK_FALSE(fixture.downloader->isBusy);
  CHECK(queueChanges == changesBeforeDestruction);
}

TEST_CASE("workshop shutdown releases idle waiters without cancelling others") {
  TestApplication app;
  ContextFixture fixture;
  fixture.downloader->isBusy = true;
  auto catalog = std::make_unique<WorkshopCatalog>(
      fixture.config.get(), fixture.downloads, [&] { return fixture.mods; });
  catalog->enqueue(DownloadPlan{.to_download = {QStringLiteral("5")}});
  REQUIRE(fixture.downloader->recordedCalls.empty());

  SECTION("shutdown wakes the idle runner") {
    run(catalog->shutdown());
    CHECK(catalog->queuedIds().isEmpty());
    catalog->enqueue(DownloadPlan{.to_download = {QStringLiteral("6")}});
    CHECK(catalog->queuedIds().isEmpty());
  }
  SECTION("destruction wakes the idle runner") {}

  catalog.reset();
  fixture.downloader->isBusy = false;
  fixture.downloads.busyChanged.notify(false);
  CHECK(fixture.downloader->cancelCalls == 0);
  CHECK(fixture.downloader->recordedCalls.empty());
}

TEST_CASE(
    "workshop cached values survive a restart and corrupt files are ignored") {
  TestApplication app;
  ContextFixture fixture;

  auto produce = []() -> QCoro::Task<CatalogResult<std::vector<int>>> {
    co_return std::vector<int>{1, 2};
  };
  auto mustNotRun = []() -> QCoro::Task<CatalogResult<std::vector<int>>> {
    FAIL("A fresh value on disk must not hit the catalog");
    co_return std::vector<int>{};
  };

  // First run persists to disk.
  {
    WorkshopCatalog first(fixture.config.get(), fixture.downloads,
                          [&] { return fixture.mods; });
    std::vector<int> out;
    const auto done = run(first.cached<std::vector<int>>(
        QStringLiteral("k"), produce, [&](const auto& v) { out = v; }, 300.0,
        /*persist=*/true));
    REQUIRE(done.has_value());
    CHECK(out == std::vector<int>{1, 2});
  }

  // Second run loads from disk without invoking produce.
  {
    WorkshopCatalog second(fixture.config.get(), fixture.downloads,
                           [&] { return fixture.mods; });
    std::vector<int> out;
    const auto done = run(second.cached<std::vector<int>>(
        QStringLiteral("k"), mustNotRun, [&](const auto& v) { out = v; }, 300.0,
        /*persist=*/true));
    REQUIRE(done.has_value());
    CHECK(out == std::vector<int>{1, 2});
  }

  // Corrupting the file causes a miss: produce is invoked again.
  const QString cacheDir =
      fixture.dir.filePath(QStringLiteral("workshop-cache"));
  for (const QFileInfo& file :
       QDir(cacheDir).entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
    REQUIRE(writeBytes(file.absoluteFilePath(), "{not json"));
  }
  {
    WorkshopCatalog third(fixture.config.get(), fixture.downloads,
                          [&] { return fixture.mods; });
    std::vector<int> out;
    const auto done = run(third.cached<std::vector<int>>(
        QStringLiteral("k"), produce, [&](const auto& v) { out = v; }, 300.0,
        /*persist=*/true));
    REQUIRE(done.has_value());
    CHECK(out == std::vector<int>{1, 2});
  }
}

TEST_CASE(
    "workshop equal cache refresh keeps the displayed mods and refreshes age") {
  TestApplication app;
  ContextFixture fixture;
  WorkshopCatalog catalog(fixture.config.get(), fixture.downloads,
                          [&] { return fixture.mods; });

  int calls = 0;
  auto produce = [&]() -> QCoro::Task<CatalogResult<std::vector<CatalogMod>>> {
    ++calls;
    co_return std::vector<CatalogMod>{modWith("1", 100)};
  };

  std::vector<std::vector<CatalogMod>> initial;
  REQUIRE(run(catalog.cached<std::vector<CatalogMod>>(
                  QStringLiteral("installed"), produce,
                  [&](const auto& v) { initial.push_back(v); }, 300.0))
              .has_value());
  CHECK(initial.size() == 1);
  CHECK(calls == 1);

  // Refresh with fresh_for = 0: produce runs, but because the value is equal,
  // show is called only once.
  std::vector<std::vector<CatalogMod>> refreshed;
  REQUIRE(run(catalog.cached<std::vector<CatalogMod>>(
                  QStringLiteral("installed"), produce,
                  [&](const auto& v) { refreshed.push_back(v); }, 0.0))
              .has_value());
  CHECK(refreshed.size() == 1);
  CHECK(refreshed[0] == initial[0]);
  CHECK(calls == 2);

  // Immediate repeat: the refreshed timestamp is fresh, so produce does not
  // run.
  std::vector<std::vector<CatalogMod>> current;
  REQUIRE(run(catalog.cached<std::vector<CatalogMod>>(
                  QStringLiteral("installed"), produce,
                  [&](const auto& v) { current.push_back(v); }, 300.0))
              .has_value());
  CHECK(current.size() == 1);
  CHECK(current[0] == initial[0]);
  CHECK(calls == 2);
}

TEST_CASE("workshop disk cache is reused in memory after first load") {
  TestApplication app;
  ContextFixture fixture;

  auto produce = []() -> QCoro::Task<CatalogResult<std::vector<CatalogMod>>> {
    co_return std::vector<CatalogMod>{modWith("1", 100)};
  };
  auto mustNotRun =
      []() -> QCoro::Task<CatalogResult<std::vector<CatalogMod>>> {
    FAIL("A fresh cached response must not hit the catalog");
    co_return std::vector<CatalogMod>{};
  };

  {
    WorkshopCatalog first(fixture.config.get(), fixture.downloads,
                          [&] { return fixture.mods; });
    REQUIRE(
        run(first.cached<std::vector<CatalogMod>>(
                QStringLiteral("installed"), produce, [](const auto&) {}, 300.0,
                /*persist=*/true))
            .has_value());
  }

  WorkshopCatalog second(fixture.config.get(), fixture.downloads,
                         [&] { return fixture.mods; });
  std::vector<CatalogMod> loaded;
  REQUIRE(run(second.cached<std::vector<CatalogMod>>(
                  QStringLiteral("installed"), mustNotRun,
                  [&](const auto& v) { loaded = v; }, 300.0, /*persist=*/true))
              .has_value());
  REQUIRE(loaded.size() == 1);

  // Corrupt the disk file now: second catalog must still serve its in-memory
  // copy.
  const QString cacheDir =
      fixture.dir.filePath(QStringLiteral("workshop-cache"));
  for (const QFileInfo& file :
       QDir(cacheDir).entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
    REQUIRE(writeBytes(file.absoluteFilePath(), "{not json"));
  }

  std::vector<CatalogMod> fromMemory;
  REQUIRE(
      run(second.cached<std::vector<CatalogMod>>(
              QStringLiteral("installed"), mustNotRun,
              [&](const auto& v) { fromMemory = v; }, 300.0, /*persist=*/true))
          .has_value());
  CHECK(fromMemory == loaded);
}

TEST_CASE("workshop catalog URL moves out of the old shared config") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  REQUIRE(writeBytes(
      dir.filePath(QStringLiteral("config.json")),
      R"({"schema_version":1,"workshop_catalog_url":"http://localhost:8787"})"));

  ConfigService service(dir.path());
  DownloadManager downloads;
  WorkshopCatalog catalog(&service, downloads,
                          [] { return QList<ListedModPtr>{}; });
  CHECK(catalog.settings().url == "http://localhost:8787");

  const auto stored = service.loadExisting<CatalogSettings>(
      QStringLiteral("plugins/workshop_catalog.json"));
  REQUIRE(stored.has_value());
  REQUIRE(stored->has_value());
  CHECK((*stored)->url == "http://localhost:8787");
}
