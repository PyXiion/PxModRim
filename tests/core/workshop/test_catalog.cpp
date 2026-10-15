#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glaze/glaze.hpp>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "../test_support.h"
#include "core/config.h"
#include "core/downloads/downloader.h"
#include "core/downloads/manager.h"
#include "core/downloads/types.h"
#include "core/file_io.h"
#include "core/models/metadata/structures.h"
#include "core/workshop/catalog.h"
#include "core/workshop/client.h"
#include "core/workshop/types.h"
#include "support.h"

using namespace px::core;
using namespace px::core::workshop;
using test_support::TestApplication;
using test_support::write_bytes;
using workshop_test::FakeCatalog;
using workshop_test::HttpRequest;
using workshop_test::HttpResponse;
using workshop_test::decode_mod;
using workshop_test::run;
using workshop_test::wire;

namespace {
struct BatchWire {
    std::vector<CatalogMod> items;
    std::vector<std::string> unavailable_ids;
};

struct IdsBatchPayload {
    std::vector<std::string> ids;
};

struct FakeDownloader : public Downloader {
    FakeDownloader(DownloadManager& manager, std::map<QString, double> synced)
        : Downloader(QStringLiteral("fake_steam"), QStringLiteral("Steam"), manager), synced_(std::move(synced)) {}

    bool is_downloading() const override { return is_busy_; }
    QSet<QString> active_ids() const override { return active_; }
    bool accepts(const QString&) const override { return true; }
    std::optional<QString> updatable_id(const ListedMod& mod) const override { return mod.published_file_id(); }
    std::optional<double> last_synced(const ListedMod& mod) const override {
        const auto id = mod.published_file_id();
        if (!id) {
            return std::nullopt;
        }
        const auto found = synced_.find(*id);
        return found == synced_.end() ? std::nullopt : std::optional<double>(found->second);
    }

    QCoro::Task<Result<DownloadResult>> download_mods(QStringList ids) override {
        recorded_calls.push_back(ids);
        co_return DownloadResult{.succeeded = ids, .failed = {}, .changed = {}};
    }

    void cancel() override {}

    std::map<QString, double> synced_;
    bool is_busy_ = false;
    QSet<QString> active_;
    std::vector<QStringList> recorded_calls;
};

ListedModPtr make_mod(const QString& path, const QString& provider = QStringLiteral("local")) {
    auto mod = std::make_shared<ListedMod>();
    REQUIRE(mod->set_mod_path(path).has_value());
    mod->provider_id = provider;
    return mod;
}

CatalogMod mod_with(std::string id, std::optional<std::int64_t> updated) {
    CatalogMod mod = decode_mod(workshop_test::kModJson);
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
        downloader = std::make_unique<FakeDownloader>(downloads, std::map<QString, double>{
            {QStringLiteral("1"), 200.0},
            {QStringLiteral("2"), 50.0},
        });
        downloads.register_downloader(*downloader);

        // Mods 1, 2, 4 are local; 3 is workshop; NotWorkshop has no numeric folder name.
        mods.append(make_mod(dir.filePath(QStringLiteral("Mods/1"))));
        mods.append(make_mod(dir.filePath(QStringLiteral("Mods/2"))));
        mods.append(make_mod(dir.filePath(QStringLiteral("Workshop/3")), QStringLiteral("workshop")));
        mods.append(make_mod(dir.filePath(QStringLiteral("Mods/4"))));
        mods.append(make_mod(dir.filePath(QStringLiteral("Mods/NotWorkshop"))));
    }
};

struct ResolvePayload {
    std::vector<std::string> ids;
    std::vector<std::string> collection_ids;
};

}  // namespace

TEST_CASE("workshop install state uses sync timestamp, not directory mtime") {
    ContextFixture fixture;
    WorkshopCatalog catalog(fixture.config.get(), fixture.downloads, [&] { return fixture.mods; });

    CHECK(catalog.install_state(mod_with("1", 100)) == InstallState::installed);
    CHECK(catalog.install_state(mod_with("2", 100)) == InstallState::outdated);
    // 3 has no sync record, so it cannot be outdated.
    CHECK(catalog.install_state(mod_with("3", 100)) == InstallState::installed);
    CHECK(catalog.install_state(mod_with("4", 100)) == InstallState::installed);
    // 5 is not installed at all.
    CHECK(catalog.install_state(mod_with("5", 100)) == InstallState::missing);
    // When updated_at is null or equal to sync, the mod is installed.
    CHECK(catalog.install_state(mod_with("2", std::nullopt)) == InstallState::installed);
    CHECK(catalog.install_state(mod_with("2", 50)) == InstallState::installed);
}

TEST_CASE("workshop plan keeps dependency order and incomplete flags") {
    TestApplication app;
    ContextFixture fixture;

    FakeCatalog server([](const HttpRequest&) -> HttpResponse {
        ResolvedDownload answer;
        answer.roots = {"5", "picked:starter"};
        for (const char* id : {"1", "2", "3", "4", "5"}) {
            CatalogMod m = mod_with(id, 100);
            m.title = std::string("Title ") + id;
            answer.items.emplace(id, std::move(m));
        }
        answer.items.emplace("picked:starter", workshop_test::decode_collection(workshop_test::kCollectionJson));
        answer.mod_ids = {"1", "2", "3", "4", "5"};
        answer.unavailable_ids = {"99"};
        answer.incomplete_collection_ids = {"picked:starter"};
        answer.is_complete = false;
        return {200, wire(answer)};
    });

    WorkshopCatalog catalog(fixture.config.get(), fixture.downloads, [&] { return fixture.mods; });
    catalog.set_base_url(server.url());

    const auto plan = run(catalog.plan({QStringLiteral("5")}, {QStringLiteral("picked:starter")}));
    REQUIRE(plan.has_value());
    CHECK(plan->to_download == QStringList{QStringLiteral("2"), QStringLiteral("5")});
    CHECK(plan->already_current == QStringList{QStringLiteral("1"), QStringLiteral("3"), QStringLiteral("4")});
    CHECK(plan->unavailable_ids == QStringList{QStringLiteral("99")});
    CHECK(plan->incomplete_collection_ids == QStringList{QStringLiteral("picked:starter")});
    CHECK_FALSE(plan->is_complete);
    CHECK(plan->titles.value(QStringLiteral("2")) == QStringLiteral("Title 2"));

    const auto payload = glz::read_json<ResolvePayload>(server.requests.back().body);
    REQUIRE(payload.has_value());
    CHECK(payload->ids == std::vector<std::string>{"5"});
    CHECK(payload->collection_ids == std::vector<std::string>{"picked:starter"});
}

TEST_CASE("workshop installed metadata uses batch") {
    TestApplication app;
    ContextFixture fixture;

    std::vector<std::string> requested_ids;
    FakeCatalog server([&](const HttpRequest& request) -> HttpResponse {
        const auto body = glz::read_json<IdsBatchPayload>(request.body);
        REQUIRE(body.has_value());
        requested_ids = body->ids;
        BatchWire answer{
            .items = {mod_with("2", 100)},
            .unavailable_ids = {"1", "3", "4"},
        };
        return {200, wire(answer)};
    });

    WorkshopCatalog catalog(fixture.config.get(), fixture.downloads, [&] { return fixture.mods; });
    catalog.set_base_url(server.url());

    const auto installed = run(catalog.installed_with_updates());
    REQUIRE(installed.has_value());
    CHECK(requested_ids == std::vector<std::string>{"1", "2", "3", "4"});
    REQUIRE(installed->size() == 1);
    CHECK(catalog.install_state(installed->front()) == InstallState::outdated);
}

TEST_CASE("workshop download delegates without side effects") {
    TestApplication app;
    ContextFixture fixture;
    WorkshopCatalog catalog(fixture.config.get(), fixture.downloads, [&] { return fixture.mods; });

    DownloadPlan plan{
        .to_download = {QStringLiteral("2")},
        .already_current = {QStringLiteral("1")},
        .titles = {{QStringLiteral("2"), QStringLiteral("Mod")}},
        .is_complete = true,
    };
    const auto result = run(catalog.download(plan));
    REQUIRE(result.has_value());
    CHECK(result->succeeded == QStringList{QStringLiteral("2")});
    REQUIRE(fixture.downloader->recorded_calls.size() == 1);
    CHECK(fixture.downloader->recorded_calls[0] == QStringList{QStringLiteral("2")});
}

TEST_CASE("workshop empty URL is disabled without creating a client") {
    TestApplication app;
    ContextFixture fixture;
    WorkshopCatalog catalog(fixture.config.get(), fixture.downloads, [&] { return fixture.mods; });
    catalog.set_base_url(QString());
    CHECK_FALSE(catalog.configured());

    const auto discover = run(catalog.discover());
    REQUIRE_FALSE(discover.has_value());
    CHECK(discover.error().message.contains(QStringLiteral("disabled")));

    const auto installed = run(catalog.installed_with_updates());
    REQUIRE_FALSE(installed.has_value());
    CHECK(installed.error().message.contains(QStringLiteral("disabled")));

    const auto plan = run(catalog.plan({QStringLiteral("1")}, {}));
    REQUIRE_FALSE(plan.has_value());
    CHECK(plan.error().message.contains(QStringLiteral("disabled")));
}

TEST_CASE("workshop configuration and installed events") {
    TestApplication app;
    ContextFixture fixture;
    WorkshopCatalog catalog(fixture.config.get(), fixture.downloads, [&] { return fixture.mods; });

    std::vector<QString> url_changes;
    int installed_invalidated = 0;
    catalog.catalog_url_changed.connect([&](const QString& url) { url_changes.push_back(url); });
    catalog.installed_changed.connect([&](std::monostate) { ++installed_invalidated; });

    catalog.set_base_url(QStringLiteral(" https://other.example/ "));
    CHECK(catalog.settings().url == "https://other.example");
    const auto stored = fixture.config->load_existing<CatalogSettings>(QStringLiteral("plugins/workshop_catalog.json"));
    REQUIRE(stored.has_value());
    REQUIRE(stored->has_value());
    CHECK((*stored)->url == "https://other.example");

    catalog.set_base_url(QString());
    CHECK_FALSE(catalog.configured());

    // Firing download_finished and manual invalidation both emit installed_changed.
    catalog.invalidate_installed();
    fixture.downloads.download_finished.notify(DownloadResult{});
    CHECK(installed_invalidated == 2);
    CHECK(url_changes == std::vector<QString>{QStringLiteral("https://other.example"), QString()});
}

TEST_CASE("workshop cached serves remembered value before refreshing") {
    TestApplication app;
    ContextFixture fixture;
    WorkshopCatalog catalog(fixture.config.get(), fixture.downloads, [&] { return fixture.mods; });

    std::vector<std::string> sequence{"first", "second", "second"};
    std::size_t next_idx = 0;
    int calls = 0;

    auto produce = [&]() -> QCoro::Task<CatalogResult<std::string>> {
        ++calls;
        co_return sequence.at(next_idx++);
    };

    auto seen = [&](double fresh_for) {
        std::vector<std::string> out;
        const auto done = run(catalog.cached<std::string>(
            QStringLiteral("k"), produce, [&](const std::string& v) { out.push_back(v); }, fresh_for));
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
    WorkshopCatalog catalog(fixture.config.get(), fixture.downloads, [&] { return fixture.mods; });

    fixture.downloader->is_busy_ = true;
    DownloadPlan first{.to_download = {QStringLiteral("5"), QStringLiteral("6")}};
    DownloadPlan second{.to_download = {QStringLiteral("6"), QStringLiteral("7")}};
    catalog.enqueue(first);
    catalog.enqueue(second);

    CHECK(catalog.queued_ids() == QSet<QString>{QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7")});
    CHECK(fixture.downloader->recorded_calls.empty());

    // Once the downloads report idle, the queue drains both batches in order.
    fixture.downloader->is_busy_ = false;
    fixture.downloads.busy_changed.notify(false);

    CHECK(fixture.downloader->recorded_calls.size() == 2);
    CHECK(fixture.downloader->recorded_calls[0] == QStringList{QStringLiteral("5"), QStringLiteral("6")});
    CHECK(fixture.downloader->recorded_calls[1] == QStringList{QStringLiteral("7")});
    CHECK(catalog.queued_ids().isEmpty());
}

TEST_CASE("workshop cached values survive a restart and corrupt files are ignored") {
    TestApplication app;
    ContextFixture fixture;

    auto produce = []() -> QCoro::Task<CatalogResult<std::vector<int>>> {
        co_return std::vector<int>{1, 2};
    };
    auto must_not_run = []() -> QCoro::Task<CatalogResult<std::vector<int>>> {
        FAIL("A fresh value on disk must not hit the catalog");
        co_return std::vector<int>{};
    };

    // First run persists to disk.
    {
        WorkshopCatalog first(fixture.config.get(), fixture.downloads, [&] { return fixture.mods; });
        std::vector<int> out;
        const auto done = run(first.cached<std::vector<int>>(
            QStringLiteral("k"), produce, [&](const auto& v) { out = v; }, 300.0, /*persist=*/true));
        REQUIRE(done.has_value());
        CHECK(out == std::vector<int>{1, 2});
    }

    // Second run loads from disk without invoking produce.
    {
        WorkshopCatalog second(fixture.config.get(), fixture.downloads, [&] { return fixture.mods; });
        std::vector<int> out;
        const auto done = run(second.cached<std::vector<int>>(
            QStringLiteral("k"), must_not_run, [&](const auto& v) { out = v; }, 300.0, /*persist=*/true));
        REQUIRE(done.has_value());
        CHECK(out == std::vector<int>{1, 2});
    }

    // Corrupting the file causes a miss: produce is invoked again.
    const QString cache_dir = fixture.dir.filePath(QStringLiteral("workshop-cache"));
    for (const QFileInfo& file : QDir(cache_dir).entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        REQUIRE(write_bytes(file.absoluteFilePath(), "{not json"));
    }
    {
        WorkshopCatalog third(fixture.config.get(), fixture.downloads, [&] { return fixture.mods; });
        std::vector<int> out;
        const auto done = run(third.cached<std::vector<int>>(
            QStringLiteral("k"), produce, [&](const auto& v) { out = v; }, 300.0, /*persist=*/true));
        REQUIRE(done.has_value());
        CHECK(out == std::vector<int>{1, 2});
    }
}

TEST_CASE("workshop equal cache refresh keeps the displayed mods and refreshes age") {
    TestApplication app;
    ContextFixture fixture;
    WorkshopCatalog catalog(fixture.config.get(), fixture.downloads, [&] { return fixture.mods; });

    int calls = 0;
    auto produce = [&]() -> QCoro::Task<CatalogResult<std::vector<CatalogMod>>> {
        ++calls;
        co_return std::vector<CatalogMod>{mod_with("1", 100)};
    };

    std::vector<std::vector<CatalogMod>> initial;
    REQUIRE(run(catalog.cached<std::vector<CatalogMod>>(
                QStringLiteral("installed"), produce, [&](const auto& v) { initial.push_back(v); }, 300.0))
                .has_value());
    CHECK(initial.size() == 1);
    CHECK(calls == 1);

    // Refresh with fresh_for = 0: produce runs, but because the value is equal, show is called only once.
    std::vector<std::vector<CatalogMod>> refreshed;
    REQUIRE(run(catalog.cached<std::vector<CatalogMod>>(
                QStringLiteral("installed"), produce, [&](const auto& v) { refreshed.push_back(v); }, 0.0))
                .has_value());
    CHECK(refreshed.size() == 1);
    CHECK(refreshed[0] == initial[0]);
    CHECK(calls == 2);

    // Immediate repeat: the refreshed timestamp is fresh, so produce does not run.
    std::vector<std::vector<CatalogMod>> current;
    REQUIRE(run(catalog.cached<std::vector<CatalogMod>>(
                QStringLiteral("installed"), produce, [&](const auto& v) { current.push_back(v); }, 300.0))
                .has_value());
    CHECK(current.size() == 1);
    CHECK(current[0] == initial[0]);
    CHECK(calls == 2);
}

TEST_CASE("workshop disk cache is reused in memory after first load") {
    TestApplication app;
    ContextFixture fixture;

    auto produce = []() -> QCoro::Task<CatalogResult<std::vector<CatalogMod>>> {
        co_return std::vector<CatalogMod>{mod_with("1", 100)};
    };
    auto must_not_run = []() -> QCoro::Task<CatalogResult<std::vector<CatalogMod>>> {
        FAIL("A fresh cached response must not hit the catalog");
        co_return std::vector<CatalogMod>{};
    };

    {
        WorkshopCatalog first(fixture.config.get(), fixture.downloads, [&] { return fixture.mods; });
        REQUIRE(run(first.cached<std::vector<CatalogMod>>(QStringLiteral("installed"), produce, [](const auto&) {}, 300.0,
                                                          /*persist=*/true))
                    .has_value());
    }

    WorkshopCatalog second(fixture.config.get(), fixture.downloads, [&] { return fixture.mods; });
    std::vector<CatalogMod> loaded;
    REQUIRE(run(second.cached<std::vector<CatalogMod>>(QStringLiteral("installed"), must_not_run,
                                                      [&](const auto& v) { loaded = v; }, 300.0, /*persist=*/true))
                .has_value());
    REQUIRE(loaded.size() == 1);

    // Corrupt the disk file now: second catalog must still serve its in-memory copy.
    const QString cache_dir = fixture.dir.filePath(QStringLiteral("workshop-cache"));
    for (const QFileInfo& file : QDir(cache_dir).entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        REQUIRE(write_bytes(file.absoluteFilePath(), "{not json"));
    }

    std::vector<CatalogMod> from_memory;
    REQUIRE(run(second.cached<std::vector<CatalogMod>>(QStringLiteral("installed"), must_not_run,
                                                      [&](const auto& v) { from_memory = v; }, 300.0, /*persist=*/true))
                .has_value());
    CHECK(from_memory == loaded);
}

TEST_CASE("workshop catalog URL moves out of the old shared config") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    REQUIRE(write_bytes(dir.filePath(QStringLiteral("config.json")),
                        R"({"schema_version":1,"workshop_catalog_url":"http://localhost:8787"})"));

    ConfigService service(dir.path());
    DownloadManager downloads;
    WorkshopCatalog catalog(&service, downloads, [] { return QList<ListedModPtr>{}; });
    CHECK(catalog.settings().url == "http://localhost:8787");

    const auto stored = service.load_existing<CatalogSettings>(QStringLiteral("plugins/workshop_catalog.json"));
    REQUIRE(stored.has_value());
    REQUIRE(stored->has_value());
    CHECK((*stored)->url == "http://localhost:8787");
}
